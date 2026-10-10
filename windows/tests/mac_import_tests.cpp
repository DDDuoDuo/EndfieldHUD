#include "mac_import_test_support.hpp"
#include <set>

namespace {
void fullImport() {
    Temporary t;
    auto e = fullExport(t.root / "export");
    e.write();
    const auto exportBefore = tree(e.root);
    const auto destination = t.root / "EndfieldHUD" / "v1";
    fs::create_directories(destination.parent_path());
    m::MacImportSession session(e.root, destination, platform());
    session.stage();
    const auto& s = session.summary();
    using S = m::MacImportStore;
    using St = m::MacImportStatus;
    check(store(s, S::archive).status == St::rejectedInvalid && store(s, S::appShortcuts).status == St::notInExport,
          "Archive without its Unicode validator is rejected, never skipped silently");
    check(!session.committable(), "A rejected store blocks commit without explicit consent");
    check(store(s, S::notes).status == St::importedWithWarnings && store(s, S::notes).records == 4 && store(s, S::notes).relinkItems == 1 &&
          hasWarning(store(s, S::notes), "missing") && hasWarning(store(s, S::notes), "unreferenced"), "Notes report: missing image kept, orphan skipped, media needs relink");
    check(store(s, S::profile).status == St::importedWithWarnings && hasWarning(store(s, S::profile), "Background image"), "Missing profile background is reported");
    check(store(s, S::reader).relinkItems == 1 && store(s, S::fileShelf).relinkItems == 1 && store(s, S::fileShelf).records == 1, "Reader book and shelf item need relinking");
    check(store(s, S::worldMap).status == St::imported && store(s, S::eventLog).records == 2 && store(s, S::calendar).records == 1, "Map/EventLog/Calendar validate");
    check(s.profileSyncLocked && hasWarning(store(s, S::account), "Sign in again"), "Account cache drives the profile sync lock and needs re-login");
    check(s.remindersNeedReconcile && !s.launchAtLogin && !s.customShortcutUntranslated, "Post-import intents are reported");
    check(store(s, S::centerLogo).status == St::importedWithWarnings && hasWarning(store(s, S::centerLogo), "unused logo"), "Selected logo imported, stale revision skipped");
    rejects([&] { (void)session.commit(); }, "Commit refuses while a store is rejected");
    const auto firstWork = session.workDirectory();
    session.discard();
    check(!fs::exists(firstWork) && !fs::exists(destination), "Discarding a reviewed import removes its private folder and writes nothing");

    // Explicit consent for the rejected Archive store; no destination existed.
    m::MacImportOptions consent; consent.acceptRejected = {S::archive};
    m::MacImportSession accepted(e.root, destination, platform(), consent);
    accepted.stage();
    check(accepted.committable(), "Explicitly accepted rejections allow commit");
    const auto result = accepted.commit();
    check(result.workRemoved && siblingsClean(destination) && result.backup.empty() && backups(destination) == 0, "Commit leaves no private work folder and no backup when nothing existed");
    check(tree(e.root) == exportBefore, "The macOS export is never modified");
    // Unchanged records are byte-identical; migrations run only in the copy.
    check(read(destination / "Notes" / "notes.sqlite3") == e.files["EndfieldCharge/Notes/notes.sqlite3"], "Notes v2 database is byte-identical");
    check(read(destination / "Notes" / "Images" / (uuid(90) + ".png")) == png1x1 && !fs::exists(destination / "Notes" / "Images" / (uuid(99) + ".png")), "Managed image copied; orphan not imported");
    check(read(destination / "Profile" / "profile.json") == e.files["EndfieldCharge/Profile/profile.json"], "Profile JSON (unknown fields, large IDs, Foundation dates) is byte-identical");
    check(read(destination / "Profile" / "Images" / (uuid(70) + ".image")) == e.files["EndfieldCharge/Profile/Images/" + uuid(70) + ".image"], "Avatar .image keeps its original encoded bytes");
    check(read(destination / "Reader" / "library.json") == e.files["EndfieldCharge/Reader/library.json"], "Reader library is byte-identical");
    check(read(destination / "Calendar" / "calendar.json") == e.files["EndfieldCharge/Calendar/calendar.json"], "Calendar is byte-identical");
    check(read(destination / "EventLog" / "events.json") == e.files["EndfieldCharge/EventLog/events.json"], "Event log is byte-identical");
    check(J::parse(read(destination / "WorldMap" / "map.json"))["version"].integer() == 4 && J::parse(read(destination / "WorldMap" / "map.json"))["futureRoot"].string() == "kept",
          "Map v2 migrated to v4 inside the stage, unknown fields kept");
    check(!fs::exists(destination / "FileShelf") && !fs::exists(destination / "Archive"), "Mac shelf references are not written as Windows records; rejected Archive writes nothing");
    check(read(destination / "CenterLogo" / "9B2D1C3A-1111-4222-8333-444455556666.png") == png1x1, "Custom center logo copied with its revision");
    {
        const auto account = J::parse(read(destination / "Account" / "profile-cache.json"));
        const auto& record = account["records"]["mainland"];
        check(record["requiresReconnect"].boolean() && record["linked"].boolean(), "Linked region is marked disconnected until reauthentication");
        check(record["roles"].array()[0]["roleID"].string() == "18446744073709551619" && record["roles"].array()[0]["bindingUID"].string() == "9007199254740993",
              "Role identifiers above 2^53 stay exact strings");
        const auto snapshot = record["snapshots"].object().begin()->second;
        check(!snapshot.contains("token") && account["futureCacheField"]["a"].integer() == 1, "Credential-like keys are stripped; other unknown fields kept");
    }
    {
        const d::SettingsStore settings(destination);
        const auto& f = settings.value().fields;
        check(f["language"].string() == "simplifiedChinese" && f["accentHex"].string() == "12ABEF" && f["hudScale"].number() == 1.25 && !f["launchAtLogin"].boolean(), "Settings mapped");
        check(f["orbipom.bestScore.v1"].integer() == 88 && f["windowsSummonShortcut"]["virtualKey"].integer() == 0xc0 && !f.contains("hasLaunched"), "Best score and hotkey intent");
        check(J::parse(read(destination / "settings.json"))["hasLaunched"] == J(true) && accepted.summary().hasLaunched, "First-run marker sits in the settings envelope beside the record");
        check(accepted.summary().hasLaunched, "Summary reports the first-run marker");
        check(!f.contains("SUEnableAutomaticChecks") && !f.contains("NSWindow Frame Settings"), "Updater and unknown keys stay out of the Windows record");
        check(read(destination / "Migration" / "mac-preferences.plist") == e.files["Preferences/io.github.endfieldcharge.EndfieldCharge.plist"], "Original preferences archived byte-for-byte");
    }
    const auto report = J::parse(read(destination / "Migration" / "import.json"));
    check(report["version"].integer() == 1 && report["manifestSHA256"].string() == m::Sha256::hex(read(e.root / "manifest.json")) && report["backup"].isNull() &&
          report["ignoredFiles"].array().size() == 1 && report["profileSyncLocked"].boolean(), "Import report records manifest digest, ignored files and intents");
    check(m::macImportCompleted(destination), "The completed import is detectable");
    // Relink ledger survives and supports explicit user decisions only.
    m::MacRelinkLedger ledger(destination);
    check(ledger.items().size() == 3 && ledger.unresolved(m::relinkNotesMedia).size() == 1 && ledger.unresolved(m::relinkReaderBook).size() == 1 &&
          ledger.unresolved(m::relinkShelfItem).size() == 1, "Ledger lists every unresolved Mac reference");
    const auto book = ledger.unresolved(m::relinkReaderBook).front();
    check(book.recordID == uuid(20) && book.lastKnownPath == "/Users/doctor/Books/endfield.epub" && book.macReference["bookmark"].string() == bookmark, "Book entry keeps the opaque Mac locator");
    ledger.markResolved(book.id, "C:\\Users\\Doctor\\Books\\endfield.epub");
    const auto shelfID = ledger.unresolved(m::relinkShelfItem).front().id;
    ledger.dismiss(shelfID);
    m::MacRelinkLedger reloaded(destination);
    check(reloaded.find(book.id)->state == m::RelinkState::resolved && *reloaded.find(book.id)->windowsPath == "C:\\Users\\Doctor\\Books\\endfield.epub" &&
          reloaded.find(shelfID)->state == m::RelinkState::dismissed && reloaded.unresolved().size() == 1, "Ledger decisions persist atomically");
    rejects([&] { reloaded.markResolved(shelfID, "C:\\x.pdf"); }, "Dismissed references are not relinked implicitly");
    rejects([&] { reloaded.markResolved(book.id, "/Users/doctor/book.epub"); }, "Relinking needs an explicit Windows path");
    // Reader relink keeps id, progress and bookmarks; the Mac locator goes.
    mod::ReaderJSONRepository readerRepository(destination / "Reader");
    auto library = readerRepository.load();
    library.books[0] = m::relinkedReaderBook(library.books[0], "C:\\Users\\Doctor\\Books\\endfield.epub");
    readerRepository.save(library);
    const auto saved = J::parse(read(destination / "Reader" / "library.json"));
    const auto& savedBook = saved["books"].array()[0];
    check(savedBook["referencePlatform"].string() == "windows" && !savedBook.contains("bookmark") && savedBook["progress"].number() == .375 &&
          savedBook["bookmarks"].array().size() == 1 && savedBook["id"].string() == uuid(20) && saved["futureLibraryField"].array().size() == 2, "Relinked book keeps progress, bookmarks and unknown fields");
    // Notes media relink through the unchanged NotesStore.
    {
        d::NotesStore notes(destination);
        auto note = *std::find_if(notes.notes().begin(), notes.notes().end(), [](const auto& n) { return n.id == uuid(4); });
        note.media = m::relinkedNoteMedia(*note.media, {"C:\\Users\\Doctor\\Pictures\\rhodes.png", "rhodes.png", "image", 640, 480});
        check(notes.upsert(note), "Relinked note media is accepted by NotesStore");
        const auto media = J::parse(*note.media);
        check(media["referencePlatform"].string() == "windows" && !media.contains("bookmark") && media["futureMediaField"].string() == "kept", "Relinked media keeps unknown fields only");
    }
    // Shelf relink keeps the original identifier and date through FileShelfStore::Creation.
    const auto creation = m::shelfRelinkCreation(*reloaded.find(shelfID));
    check(creation.id() == uuid(60) && creation.timestamp() == 700000000.75, "Shelf relink keeps identifier and Foundation date");
    {
        d::FileShelfStore::Platform shelfPlatform;
        auto metadata = [](std::string path, std::uint8_t object) {
            d::ShelfFileMetadata value;
            value.windowsPath = std::move(path); value.name = "report.pdf"; value.typeDescription = "PDF document"; value.byteCount = 1234;
            value.identity.objectID[0] = object; value.identity.volumeSerial = 7;
            return value;
        };
        shelfPlatform.acquireImport = [&](std::string_view token) { return d::ShelfFileAccess(metadata(std::string(token), token.back() == 'f' ? 1 : 2), [] {}); };
        shelfPlatform.resolve = [&](const d::ShelfRecord& record) { return d::ShelfFileAccess(metadata(record.windowsPath, record.windowsPath.back() == 'f' ? 1 : 2), [] {}); };
        m::ShelfRelinkSlot slot;
        d::FileShelfStore shelf(destination, shelfPlatform, slot.creation());
        // The ledger kept the item; arm the slot only around the explicit pick.
        auto item = *reloaded.find(shelfID);
        slot.arm(item);
        const std::string picked = "C:\\Users\\Doctor\\Desktop\\report.pdf";
        check(shelf.add(std::span<const std::string>(&picked, 1)) == 1 && !slot.armed(), "Armed relink add consumes the slot once");
        slot.disarm();
        check(shelf.items().size() == 1 && shelf.items()[0].id == uuid(60) && shelf.items()[0].createdAt == 700000000.75, "Relinked shelf item keeps its Mac identifier and date");
        const std::string other = "C:\\Users\\Doctor\\Desktop\\notes.txt";
        check(shelf.add(std::span<const std::string>(&other, 1)) == 1 && shelf.items()[1].id != uuid(60), "Ordinary adds get fresh identifiers");
    }
}
void migrationsAndRejections() {
    using S = m::MacImportStore;
    using St = m::MacImportStatus;
    {   // Notes v1 migrates in the stage; newer JSON/SQLite schemas reject without touching anything.
        Temporary t;
        Export e{t.root / "export"};
        e.add("EndfieldCharge/Notes/notes.sqlite3", legacyNotesV1());
        e.add("EndfieldCharge/Reader/library.json", R"({"version":2,"books":[]})");
        e.add("EndfieldCharge/WorldMap/map.json", "{broken");
        e.add("EndfieldCharge/FileShelf/shelf.json", shelfJSON(2));
        e.add("EndfieldCharge/Calendar/calendar.json", calendarJSON(2));
        e.write();
        const auto destination = t.root / "dest";
        m::MacImportOptions options; options.acceptRejected = {S::reader, S::worldMap, S::fileShelf, S::calendar};
        m::MacImportSession session(e.root, destination, platform(), options);
        session.stage();
        const auto& s = session.summary();
        check(store(s, S::notes).status == St::imported && store(s, S::notes).records == 1, "Legacy v1 Notes migrate inside the stage");
        check(store(s, S::reader).status == St::rejectedNewer && store(s, S::fileShelf).status == St::rejectedNewer && store(s, S::calendar).status == St::rejectedNewer,
              "Newer JSON schemas are rejected as newer");
        check(store(s, S::worldMap).status == St::rejectedInvalid && store(s, S::settings).status == St::notInExport, "Malformed JSON rejects; absent stores are reported");
        session.commit();
        sqlite3* db{};
        check(sqlite3_open_v2((destination / "Notes" / "notes.sqlite3").string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK, "Open migrated Notes");
        sqlite3_stmt* statement{};
        sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &statement, nullptr);
        sqlite3_step(statement);
        const auto version = sqlite3_column_int(statement, 0);
        sqlite3_finalize(statement);
        sqlite3_close(db);
        check(version == 2, "Imported legacy Notes database is version 2");
        check(read(e.root / "EndfieldCharge" / "Notes" / "notes.sqlite3") == e.files["EndfieldCharge/Notes/notes.sqlite3"], "Legacy source database stays untouched");
        check(!fs::exists(destination / "Reader") && !fs::exists(destination / "WorldMap"), "Rejected stores write nothing");
    }
    {   // Notes user_version 3: newer, rejected before NotesStore can touch it.
        Temporary t;
        Export e{t.root / "export"};
        e.add("EndfieldCharge/Notes/notes.sqlite3", notesDatabase(t.root, 3));
        e.sqlite["EndfieldCharge/Notes/notes.sqlite3"] = 3;
        e.write();
        m::MacImportSession session(e.root, t.root / "dest", platform());
        session.stage();
        check(store(session.summary(), S::notes).status == St::rejectedNewer && !session.committable(), "Newer Notes database rejects the whole import by default");
    }
    {   // A live journal beside the database means the export was not consistent.
        Temporary t;
        Export e{t.root / "export"};
        e.add("EndfieldCharge/Notes/notes.sqlite3", notesDatabase(t.root, 2, false));
        e.write();
        write(e.root / "EndfieldCharge" / "Notes" / "notes.sqlite3-journal", "hot");
        m::MacImportSession session(e.root, t.root / "dest", platform());
        session.stage();
        check(store(session.summary(), S::notes).status == St::rejectedInvalid && store(session.summary(), S::notes).detail.find("journal") != std::string::npos, "Hot journal rejects Notes");
    }
    {   // Corrupt SQLite fails the full integrity check.
        Temporary t;
        Export e{t.root / "export"};
        auto bytes = notesDatabase(t.root, 2, false);
        for (std::size_t i = 4096; i < bytes.size() && i < 4096 + 512; ++i) bytes[i] = static_cast<char>(0xA5);
        e.add("EndfieldCharge/Notes/notes.sqlite3", bytes);
        e.write();
        m::MacImportSession session(e.root, t.root / "dest", platform());
        session.stage();
        check(store(session.summary(), S::notes).status == St::rejectedInvalid, "Damaged database is rejected");
    }
}
void wholeImportFailures() {
    auto attempt = [](const std::function<void(Export&)>& mutate, const std::string& message, bool afterWrite = false, std::string_view reason = {}) {
        Temporary t;
        auto e = fullExport(t.root / "export");
        if (!afterWrite) mutate(e);
        e.write();
        if (afterWrite) mutate(e);
        const auto destination = t.root / "dest";
        fs::create_directories(destination / "Notes");
        write(destination / "Notes" / "keep.txt", "existing Windows data");
        const auto before = tree(destination);
        const auto error = rejects([&] { m::MacImportSession session(e.root, destination, platform()); session.stage(); }, message);
        check(tree(destination) == before && siblingsClean(destination) && backups(destination) == 0, message + " leaves the destination untouched (" + error + ")");
        check(reason.empty() || error.find(reason) != std::string::npos, message + " fails for the expected reason: " + error);
    };
    attempt([](Export& e) { write(e.root / "EndfieldCharge" / "Reader" / "library.json", "{tampered}"); }, "Digest mismatch", true, "differs from the manifest");
    attempt([](Export& e) { fs::remove(e.root / "EndfieldCharge" / "Calendar" / "calendar.json"); }, "Missing listed file", true);
    attempt([](Export& e) { e.extraManifest = J::Object{{"version", 2}}; }, "Newer exporter");
    attempt([](Export& e) { write(e.root / "manifest.json", R"({"format":"EndfieldHUD.macExport","version":1,"source":{"bundleIdentifier":"io.github.endfieldcharge.EndfieldCharge"},"exportedAt":1,"files":[{"path":"EndfieldCharge/../x","bytes":1,"sha256":")" + std::string(64, 'a') + "\"}]}"); }, "Path traversal", true);
    attempt([](Export& e) { write(e.root / "manifest.json", R"({"format":"EndfieldHUD.macExport","version":1,"source":{"bundleIdentifier":"io.github.endfieldcharge.EndfieldCharge"},"exportedAt":1,"files":[{"path":"EndfieldCharge\\Notes","bytes":1,"sha256":")" + std::string(64, 'a') + "\"}]}"); }, "Backslash path", true);
    attempt([](Export& e) { write(e.root / "manifest.json", R"({"format":"EndfieldHUD.macExport","version":1,"source":{"bundleIdentifier":"com.example.other"},"exportedAt":1,"files":[]})"); }, "Foreign bundle", true);
    // Every listed file is verified, including files no store reads.
    attempt([](Export& e) { write(e.root / "EndfieldCharge" / "Unknown" / "future.bin", "futurf"); }, "Same-size tampering of a file no store reads", true, "does not match its manifest digest");
    attempt([](Export& e) { auto bytes = png1x1; bytes[40] = static_cast<char>(bytes[40] ^ 1); write(e.root / "EndfieldCharge" / "Notes" / "Images" / (uuid(99) + ".png"), bytes); },
            "Tampered unreferenced image", true, "does not match its manifest digest");
    attempt([](Export& e) { fs::remove(e.root / "EndfieldCharge" / "CenterLogo" / "00000000-0000-4000-8000-000000000001.png"); }, "Missing unused logo revision", true, "Exported file is missing");
#ifndef _WIN32
    attempt([](Export& e) { fs::remove(e.root / "EndfieldCharge" / "Calendar" / "calendar.json"); fs::create_symlink("/etc/hosts", e.root / "EndfieldCharge" / "Calendar" / "calendar.json"); }, "Symbolic link in export", true);
#endif
    {   // Cancellation between bounded steps removes the private folder.
        Temporary t;
        auto e = fullExport(t.root / "export");
        e.write();
        auto p = platform();
        int steps = 0;
        p.cancelled = [&] { return ++steps > 3; };
        const auto destination = t.root / "dest";
        rejects([&] { m::MacImportSession session(e.root, destination, p); session.stage(); }, "Cancellation stops staging");
        check(siblingsClean(destination) && !fs::exists(destination), "Cancelled import leaves nothing behind");
    }
}
void existingDestination() {
    using S = m::MacImportStore;
    Temporary t;
    auto e = fullExport(t.root / "export");
    e.files.erase("EndfieldCharge/Notes/notes.sqlite3");
    e.sqlite.erase("EndfieldCharge/Notes/notes.sqlite3");
    e.files.erase("EndfieldCharge/Notes/Images/" + uuid(90) + ".png");
    e.files.erase("EndfieldCharge/Notes/Images/" + uuid(99) + ".png");
    e.write();
    const auto destination = t.root / "EndfieldHUD";
    {   // A Windows installation used before the import.
        d::SettingsStore settings(destination);
        auto value = settings.value();
        value.set("windowsDisplayID", "\\\\?\\DISPLAY#2");
        value.set("windowsDisplayName", "Desk");
        value.set("orbipom.bestScore.v1", std::int64_t{200});
        value.set("windowsSummonShortcut", J::Object{{"virtualKey", std::int64_t{0x48}}, {"modifiers", std::int64_t{3}}});
        settings.update(value);
        d::NotesStore notes(destination);
        notes.upsert(d::Note::textNote("Windows-only note"));
        write(destination / "FileShelf" / "shelf.json", R"({"version":1,"items":[]})");
        write(destination / "Reader" / "library.json", R"({"version":1,"books":[]})");
    }
    const auto before = tree(destination);
    auto attemptFault = [&](m::MacImportCommitStep failing) {
        m::MacImportOptions options;
        options.acceptRejected = {S::archive};
        options.beforeCommitStep = [failing](m::MacImportCommitStep step) { if (step == failing) throw std::runtime_error("injected commit failure"); };
        m::MacImportSession session(e.root, destination, platform(), options);
        session.stage();
        if (failing == m::MacImportCommitStep::removeWork) {
            const auto result = session.commit();
            check(!result.workRemoved && fs::exists(result.backup), "A cleanup failure after activation keeps the committed import");
            return result;
        }
        rejects([&] { (void)session.commit(); }, "Injected commit failure propagates");
        check(tree(destination) == before && siblingsClean(destination) && backups(destination) == 0, "Rollback restores the exact previous data root");
        return m::MacImportCommitResult{};
    };
    attemptFault(m::MacImportCommitStep::refresh);
    attemptFault(m::MacImportCommitStep::carry);
    attemptFault(m::MacImportCommitStep::report);
    attemptFault(m::MacImportCommitStep::backupDestination);
    attemptFault(m::MacImportCommitStep::activateStage);
    const auto result = attemptFault(m::MacImportCommitStep::removeWork);
    m::files::removeTree(result.destination.parent_path() / [&] {
        for (const auto& entry : fs::directory_iterator(destination.parent_path()))
            if (entry.path().filename().string().find(".import-") != std::string::npos) return entry.path().filename();
        return fs::path("missing");
    }());
    check(tree(result.backup) == before && backups(destination) == 1, "The previous root is kept intact as a timestamped backup");
    {
        const d::SettingsStore settings(destination);
        const auto& f = settings.value().fields;
        check(f["windowsDisplayID"].string() == "\\\\?\\DISPLAY#2" && f["windowsDisplayName"].string() == "Desk" && f["language"].string() == "simplifiedChinese",
              "Windows-only display choice survives; macOS settings are applied");
        check(f["orbipom.bestScore.v1"].integer() == 200, "The higher best score is kept");
        check(f["windowsSummonShortcut"]["virtualKey"].integer() == 0x48 && f["windowsSummonShortcut"]["modifiers"].integer() == 3,
              "A hotkey chosen on this PC wins over the default derived from macOS");
    }
    check(d::NotesStore(destination).notes().size() == 1 && read(destination / "FileShelf" / "shelf.json") == R"({"version":1,"items":[]})",
          "Stores absent from the export (Notes, Windows shelf) are carried over unchanged");
    check(J::parse(read(destination / "Reader" / "library.json"))["books"].array().size() == 1, "Imported Reader library replaces the Windows one (old copy in backup)");
    const auto report = J::parse(read(destination / "Migration" / "import.json"));
    check(report["backup"].isString(), "Report names the backup folder");
    // A second import is refused unless the user explicitly chooses replace.
    rejects([&] { m::MacImportSession again(e.root, destination, platform()); again.stage(); }, "Second import is refused by default");
    m::MacImportOptions replace; replace.replaceExistingImport = true; replace.acceptRejected = {S::archive};
    m::MacImportSession again(e.root, destination, platform(), replace);
    again.stage();
    check(again.summary().replacesPreviousImport, "Replacement is explicit");
    again.commit();
    check(backups(destination) == 2 && siblingsClean(destination), "Replacing an import makes a fresh backup; none are deleted");
}
void codecs() {
    // Account cache: Swift synthesized Codable rules.
    rejects([] { (void)m::importMacAccountCache(R"({"version":1,"region":"mainland","header":"x","syncProfile":true,"syncAvatar":false})"); }, "Missing records rejects");
    rejects([] { (void)m::importMacAccountCache(R"({"version":1,"region":"moon","header":"x","syncProfile":true,"syncAvatar":false,"records":{}})"); }, "Unknown region rejects");
    try { (void)m::importMacAccountCache(R"({"version":2,"region":"mainland","header":"x","syncProfile":true,"syncAvatar":false,"records":{}})"); check(false, "Newer account cache"); }
    catch (const m::MacImportError& e) { check(e.code() == m::MacImportErrorCode::newerVersion, "Newer account cache is reported as newer"); }
    const auto unsynced = m::importMacAccountCache(R"({"version":1,"region":"global","header":"x","syncProfile":false,"syncAvatar":false,"records":{"global":{"linked":false,"requiresReconnect":false,"roles":[],"snapshots":{}}}})");
    check(!unsynced.profileSyncLocked && unsynced.disconnectedRegions.empty(), "Unlinked cache does not lock the profile");
    rejects([] { (void)m::importMacAccountCache(R"({"version":1,"region":"mainland","header":"x","syncProfile":true,"syncAvatar":false,"records":{"mainland":{"linked":true,"requiresReconnect":false,"roles":[{"region":"mainland","game":"endfield","bindingUID":"1","roleID":"2","isDefault":true,"isAvailable":true,"level":1.5}],"snapshots":{"k":{"role":{"region":"mainland","game":"endfield","bindingUID":"1","roleID":"2","isDefault":true,"isAvailable":true},"observedAt":1,"level":1.5}}}}})"); },
            "Fractional Int rejects like JSONDecoder");
    // Shelf: FileShelfStore.decode rules.
    rejects([] { (void)m::decodeMacShelf(R"({"version":1,"items":[{"id":"00000000-0000-4000-8000-000000000001","bookmark":"","isSecurityScoped":false,"lastKnownPath":"/a","name":"a","typeDescription":"","isDirectory":false,"createdAt":1}]})"); }, "Empty bookmark rejects");
    rejects([] { (void)m::decodeMacShelf(R"({"version":1,"items":[{"id":"00000000-0000-4000-8000-000000000001","bookmark":"YQ==","isSecurityScoped":false,"lastKnownPath":"a","name":"a","typeDescription":"","isDirectory":false,"createdAt":1}]})"); }, "Relative path rejects");
    check(m::decodeMacShelf(R"({"version":1,"items":[]})").empty(), "Empty shelf decodes");
    // Images and paths.
    check(m::pngHeader(png1x1)->width == 1 && m::sniffImage(png1x1) == m::ImageSignature::png && m::sniffImage("\xFF\xD8\xFF") == m::ImageSignature::jpeg &&
          m::sniffImage(std::string("\0\0\0\x18" "ftypheic", 12)) == m::ImageSignature::heif, "Image signatures");
    check(m::macManagedImageName(uuid(1) + ".png", false) && m::macManagedImageName(uuid(1) + ".image", true) && !m::macManagedImageName(uuid(1) + ".image", false) &&
          !m::macManagedImageName("00000000-0000-4000-8000-00000000000a.png", false), "Managed image names are canonical uppercase UUIDs");
    for (const char* bad : {"", "EndfieldCharge", "Other/x", "EndfieldCharge/../x", "EndfieldCharge//x", "EndfieldCharge/a:b", "EndfieldCharge/CON", "EndfieldCharge/nul.txt", "EndfieldCharge/x.", "/EndfieldCharge/x"})
        check(!m::validExportPath(bad), std::string("Unsafe export path rejected: ") + bad);
    check(m::validExportPath("EndfieldCharge/Notes/Images/" + uuid(1) + ".png") && m::validExportPath("Preferences/io.github.endfieldcharge.EndfieldCharge.plist"), "Expected export paths accepted");
}

void streamCopy() {
    Temporary t;
    std::string bytes(3000, '\0');
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<char>(i * 7 + 3);
    write(t.root / "a.bin", bytes);
    {
        m::files::StreamCopy copy(t.root / "a.bin", t.root / "out" / "a.bin", bytes.size(), m::files::StreamCopy::Expected{bytes.size(), m::Sha256::hex(bytes)});
        std::size_t steps{};
        while (!copy.done()) { check(copy.advance(1000) <= 1000, "A copy step never exceeds its budget"); ++steps; }
        check(steps == 3 && copy.processed() == bytes.size() && read(t.root / "out" / "a.bin") == bytes, "Chunked verified copy is exact and ends with its last full chunk");
    }
    {
        m::files::StreamCopy copy(t.root / "a.bin", t.root / "out" / "b.bin", bytes.size(), m::files::StreamCopy::Expected{bytes.size(), m::Sha256::hex("other")});
        try { while (!copy.done()) copy.advance(1000); check(false, "Digest mismatch must fail"); }
        catch (const m::MacImportError& error) { check(error.code() == m::MacImportErrorCode::corrupt, "A digest mismatch is corruption"); }
        check(!fs::exists(t.root / "out" / "b.bin"), "A mismatching copy leaves no file");
    }
    {
        m::files::StreamCopy copy(t.root / "a.bin", t.root / "out" / "c.bin", 2999, m::files::StreamCopy::Expected{2999, m::Sha256::hex(bytes.substr(0, 2999))});
        rejects([&] { while (!copy.done()) copy.advance(4096); }, "A file longer than its manifest entry fails");
        check(!fs::exists(t.root / "out" / "c.bin"), "Longer source leaves no file");
    }
    {
        { m::files::StreamCopy copy(t.root / "a.bin", t.root / "out" / "d.bin", bytes.size()); check(copy.advance(1000) == 1000 && fs::exists(t.root / "out" / "d.bin"), "Partial copy in progress"); }
        check(!fs::exists(t.root / "out" / "d.bin"), "An abandoned copy removes its partial file");
    }
    {
        m::files::StreamCopy verifyOnly(t.root / "a.bin", {}, bytes.size(), m::files::StreamCopy::Expected{bytes.size(), m::Sha256::hex(bytes)});
        while (!verifyOnly.done()) verifyOnly.advance(512);
        m::files::StreamCopy limited(t.root / "a.bin", t.root / "out" / "e.bin", 2000);
        try { while (!limited.done()) limited.advance(4096); check(false, "Limit must apply"); }
        catch (const m::MacImportError& error) { check(error.code() == m::MacImportErrorCode::tooLarge, "Unverified copies are bounded"); }
        check(!fs::exists(t.root / "out" / "e.bin"), "A refused copy leaves no file");
    }
    {
        write(t.root / "empty.bin", "");
        m::files::StreamCopy copy(t.root / "empty.bin", t.root / "out" / "empty.bin", 0, m::files::StreamCopy::Expected{0, m::Sha256::hex("")});
        copy.advance(1);
        check(copy.done() && fs::exists(t.root / "out" / "empty.bin"), "Empty files complete in one step");
    }
    {
        write(t.root / "tree" / "b" / "two.txt", "22");
        write(t.root / "tree" / "a.txt", "1");
        fs::create_directories(t.root / "tree" / "empty");
        const auto planned = m::files::planTree(t.root / "tree", t.root / "copy", 3);
        check(planned.size() == 2 && planned[0].bytes == 1 && planned[1].bytes == 2 && fs::is_directory(t.root / "copy" / "empty") && fs::is_directory(t.root / "copy" / "b"),
              "Tree planning lists files in order and creates every directory");
        rejects([&] { (void)m::files::planTree(t.root / "tree", {}, 2); }, "Tree planning is bounded");
    }
}
// Staging and commit never run one unbounded job on the shared worker.
void boundedSteps() {
    using S = m::MacImportStore;
    Temporary t;
    auto e = fullExport(t.root / "export");
    e.add("EndfieldCharge/Unknown/large.bin", std::string(5000, 'x')); // verified in place, never copied
    e.write();
    const auto destination = t.root / "EndfieldHUD";
    write(destination / "WindowsOnly" / "blob.bin", std::string(4000, 'w'));
    write(destination / "WindowsOnly" / "nested" / "small.txt", "s");
    m::MacImportOptions options;
    options.acceptRejected = {S::archive};
    options.stepBytes = 1024;
    options.stepImages = 1;
    m::MacImportSession session(e.root, destination, platform(), options);
    std::size_t steps{};
    std::uint64_t largest{};
    auto last = session.progress();
    for (bool more = true; more; ++steps) {
        more = session.stageNext();
        const auto now = session.progress();
        largest = std::max<std::uint64_t>(largest, now.verifiedBytes - last.verifiedBytes);
        last = now;
    }
    check(largest <= 1024 && last.verifiedBytes == last.exportBytes && last.phase == m::MacImportPhase::staged,
          "Every listed byte is verified, at most stepBytes per step");
    // open + ceil(export / 1024) chunks at least + 12 stores + 2 one-image batches
    check(steps >= 1 + last.exportBytes / 1024 + m::macImportStores.size() + 2, "Verification and image checks are split into bounded steps");
    check(!fs::exists(session.workDirectory() / "source" / "EndfieldCharge" / "Unknown"), "Files no store reads are verified in place, not copied");
    largest = 0;
    std::size_t commitSteps{};
    for (bool more = true; more; ++commitSteps) {
        more = session.commitNext();
        const auto now = session.progress();
        largest = std::max<std::uint64_t>(largest, now.carriedBytes - last.carriedBytes);
        last = now;
    }
    check(largest <= 1024 && last.carriedBytes == 4001 && last.carryBytes == 4001 && commitSteps >= 4 + 4 && session.committed(),
          "Existing Windows data is carried at most stepBytes per step");
    check(read(destination / "WindowsOnly" / "blob.bin") == std::string(4000, 'w') && read(destination / "WindowsOnly" / "nested" / "small.txt") == "s",
          "Carried Windows data is exact");
}
// Commit re-reads the root after the owners closed their stores, so changes
// saved while the import was being reviewed are never lost.
void reviewChangesKept() {
    using S = m::MacImportStore;
    Temporary t;
    auto e = fullExport(t.root / "export");
    e.files.erase("EndfieldCharge/Notes/notes.sqlite3");
    e.sqlite.erase("EndfieldCharge/Notes/notes.sqlite3");
    e.write();
    const auto destination = t.root / "EndfieldHUD";
    {
        d::SettingsStore settings(destination);
        auto value = settings.value();
        value.set("windowsDisplayID", "\\\\?\\DISPLAY#1");
        settings.update(value);
        d::NotesStore notes(destination);
        auto note = d::Note::textNote("before review");
        note.id = uuid(500);
        notes.upsert(note);
    }
    m::MacImportOptions options; options.acceptRejected = {S::archive};
    m::MacImportSession session(e.root, destination, platform(), options);
    session.stage();
    {   // The app keeps running during the review.
        d::NotesStore notes(destination);
        auto note = notes.notes().front();
        note.text = "edited during review";
        notes.upsert(note);
        d::NotesStore(destination).upsert(d::Note::textNote("added during review"));
        d::SettingsStore settings(destination);
        auto value = settings.value();
        value.set("windowsDisplayID", "\\\\?\\DISPLAY#2");
        value.set("windowsSummonShortcut", J::Object{{"virtualKey", std::int64_t{0x48}}, {"modifiers", std::int64_t{3}}});
        settings.update(value);
        write(destination / "WindowsOnly" / "late.txt", "saved during review");
    }
    const auto result = session.commit();
    const auto notes = d::NotesStore(destination).notes();
    check(notes.size() == 2 && std::any_of(notes.begin(), notes.end(), [](const auto& n) { return n.text == "edited during review"; }),
          "Notes saved during the review are carried");
    const d::SettingsStore settings(destination);
    check(settings.value().fields["windowsDisplayID"].string() == "\\\\?\\DISPLAY#2" && settings.value().fields["windowsSummonShortcut"]["virtualKey"].integer() == 0x48 &&
          settings.value().fields["language"].string() == "simplifiedChinese", "Windows-only settings chosen during the review win; macOS settings apply");
    check(!session.summary().customShortcutUntranslated && read(destination / "WindowsOnly" / "late.txt") == "saved during review", "Late files and the chosen hotkey are kept");
    check(d::NotesStore(result.backup).notes().size() == 2, "The backup is the root as it was at commit");
}
// A repeated import keeps earlier relink entries for data it does not replace.
void earlierRelinkDecisions() {
    using S = m::MacImportStore;
    Temporary t;
    auto first = fullExport(t.root / "export1");
    first.write();
    const auto destination = t.root / "EndfieldHUD";
    m::MacImportOptions options; options.acceptRejected = {S::archive};
    { m::MacImportSession session(first.root, destination, platform(), options); session.stage(); session.commit(); }
    std::string shelfEntry, bookEntry, oldMedia;
    {
        m::MacRelinkLedger ledger(destination);
        shelfEntry = ledger.unresolved(m::relinkShelfItem).front().id;
        bookEntry = ledger.unresolved(m::relinkReaderBook).front().id;
        oldMedia = ledger.unresolved(m::relinkNotesMedia).front().id;
        ledger.dismiss(shelfEntry);
        ledger.markResolved(bookEntry, "C:\\Users\\Doctor\\Books\\endfield.epub");
    }
    auto second = fullExport(t.root / "export2");
    second.files.erase("EndfieldCharge/FileShelf/shelf.json");
    second.add("EndfieldCharge/Reader/library.json", R"({"version":2,"books":[]})");
    second.write();
    m::MacImportOptions replace; replace.replaceExistingImport = true; replace.acceptRejected = {S::archive, S::reader};
    m::MacImportSession session(second.root, destination, platform(), replace);
    session.stage();
    check(store(session.summary(), S::reader).status == m::MacImportStatus::rejectedNewer && store(session.summary(), S::fileShelf).status == m::MacImportStatus::notInExport,
          "Second export: newer Reader rejected, no shelf");
    session.commit();
    const m::MacRelinkLedger ledger(destination);
    check(ledger.find(shelfEntry) && ledger.find(shelfEntry)->state == m::RelinkState::dismissed, "A Mac shelf item and its decision survive an import without a shelf");
    check(ledger.find(bookEntry) && ledger.find(bookEntry)->state == m::RelinkState::resolved, "Books of a carried Reader library keep their relink decision");
    const auto media = std::count_if(ledger.items().begin(), ledger.items().end(), [](const auto& item) { return item.store == m::relinkNotesMedia; });
    check(!ledger.find(oldMedia) && media == 1, "Entries of replaced stores come only from the new export");
    check(std::any_of(session.summary().warnings.begin(), session.summary().warnings.end(), [](const auto& w) { return w.find("earlier relink") != std::string::npos; }),
          "Kept entries are reported");
    check(J::parse(read(destination / "Migration" / "import.json"))["warnings"].array().size() == 1, "The report records import-wide warnings");
}
void diskSpaceAndCancellation() {
    using S = m::MacImportStore;
    {   // Not enough room for the copies: nothing is written.
        Temporary t;
        auto e = fullExport(t.root / "export");
        e.write();
        auto p = platform();
        p.availableBytes = [](const fs::path&) { return std::optional<std::uint64_t>(1024); };
        const auto destination = t.root / "dest";
        const auto error = rejects([&] { m::MacImportSession session(e.root, destination, p); session.stage(); }, "Insufficient space is refused before copying");
        check(error.find("free disk space") != std::string::npos && siblingsClean(destination) && !fs::exists(destination), "Space preflight leaves nothing behind");
    }
    {   // Enough at staging, not for the carried data at commit: root untouched.
        Temporary t;
        auto e = fullExport(t.root / "export");
        e.write();
        const auto destination = t.root / "dest";
        write(destination / "WindowsOnly" / "blob.bin", "windows");
        const auto before = tree(destination);
        auto p = platform();
        int calls = 0;
        p.availableBytes = [&](const fs::path&) { return std::optional<std::uint64_t>(++calls == 1 ? (1ull << 40) : 1024); };
        m::MacImportOptions options; options.acceptRejected = {S::archive};
        m::MacImportSession session(e.root, destination, p, options);
        session.stage();
        rejects([&] { (void)session.commit(); }, "Insufficient space at commit is refused");
        check(calls == 2 && tree(destination) == before && siblingsClean(destination) && backups(destination) == 0, "A refused commit leaves the root exactly as it was");
    }
    {   // Cancellation is honoured during commit until activation.
        Temporary t;
        auto e = fullExport(t.root / "export");
        e.write();
        const auto destination = t.root / "dest";
        write(destination / "WindowsOnly" / "blob.bin", "windows");
        const auto before = tree(destination);
        bool cancel = false;
        auto p = platform();
        p.cancelled = [&] { return cancel; };
        m::MacImportOptions options; options.acceptRejected = {S::archive};
        m::MacImportSession session(e.root, destination, p, options);
        session.stage();
        check(session.commitNext(), "Commit refresh step");
        cancel = true;
        try { (void)session.commitNext(); check(false, "Cancelled commit must stop"); }
        catch (const m::MacImportError& error) { check(error.code() == m::MacImportErrorCode::cancelled, "Cancellation is reported"); }
        check(tree(destination) == before && siblingsClean(destination) && backups(destination) == 0 && !session.committed(), "A cancelled commit changes nothing");
    }
    {   // The data root appearing during review is backed up, never overwritten.
        Temporary t;
        auto e = fullExport(t.root / "export");
        e.write();
        const auto destination = t.root / "late";
        m::MacImportOptions options; options.acceptRejected = {S::archive};
        m::MacImportSession session(e.root, destination, platform(), options);
        session.stage();
        check(!session.summary().destinationExisted, "No root at staging");
        write(destination / "WindowsOnly" / "first-run.txt", "created while reviewing");
        const auto result = session.commit();
        check(!result.backup.empty() && read(result.backup / "WindowsOnly" / "first-run.txt") == "created while reviewing" &&
              read(destination / "WindowsOnly" / "first-run.txt") == "created while reviewing", "A root created during review is backed up and carried");
    }
}

// After a crash between the record write and markResolved, or after the user
// deleted a record, owners bring the ledger in line from their own store.
void ledgerReconcile() {
    using S = m::MacImportStore;
    Temporary t;
    auto e = fullExport(t.root / "export");
    e.write();
    const auto destination = t.root / "EndfieldHUD";
    m::MacImportOptions options; options.acceptRejected = {S::archive};
    { m::MacImportSession session(e.root, destination, platform(), options); session.stage(); session.commit(); }
    const std::string picked = "C:\\Users\\Doctor\\Books\\endfield.epub";
    {   // The Reader owner saved the relinked book, then the app stopped.
        mod::ReaderJSONRepository repository(destination / "Reader");
        auto library = repository.load();
        library.books[0] = m::relinkedReaderBook(library.books[0], picked);
        repository.save(library);
        d::NotesStore notes(destination); // the user deleted the note with Mac media
        check(notes.remove(uuid(4)), "Delete the imported media note");
    }
    m::MacRelinkLedger ledger(destination);
    const auto book = ledger.unresolved(m::relinkReaderBook).front().id;
    const auto media = ledger.unresolved(m::relinkNotesMedia).front().id;
    const auto shelf = ledger.unresolved(m::relinkShelfItem).front().id;
    const auto library = mod::ReaderJSONRepository(destination / "Reader").load();
    const auto changedBooks = ledger.reconcile(m::relinkReaderBook, [&](const m::MacRelinkItem& item) {
        const auto found = std::find_if(library.books.begin(), library.books.end(), [&](const auto& b) { return b.id == item.recordID; });
        if (found == library.books.end()) return m::RelinkRecordCheck{m::RelinkRecordState::missing, {}};
        if (found->platform == mod::ReaderReferencePlatform::windows) return m::RelinkRecordCheck{m::RelinkRecordState::relinked, found->windowsPath};
        return m::RelinkRecordCheck{};
    });
    const auto notes = d::NotesStore(destination).notes();
    const auto changedNotes = ledger.reconcile(m::relinkNotesMedia, [&](const m::MacRelinkItem& item) {
        const auto found = std::find_if(notes.begin(), notes.end(), [&](const auto& n) { return n.id == item.recordID; });
        return found == notes.end() ? m::RelinkRecordCheck{m::RelinkRecordState::missing, {}} : m::RelinkRecordCheck{};
    });
    const auto unchanged = ledger.reconcile(m::relinkShelfItem, [](const m::MacRelinkItem&) { return m::RelinkRecordCheck{}; });
    const m::MacRelinkLedger reloaded(destination);
    check(changedBooks == 1 && reloaded.find(book)->state == m::RelinkState::resolved && *reloaded.find(book)->windowsPath == picked,
          "A record relinked before a crash resolves its entry with the record's own path");
    check(changedNotes == 1 && reloaded.find(media)->state == m::RelinkState::dismissed, "An entry whose record was deleted is dismissed");
    check(unchanged == 0 && reloaded.find(shelf)->state == m::RelinkState::unresolved, "Entries still holding a Mac reference stay unresolved");
    rejects([&] { m::MacRelinkLedger again(destination);
                  (void)again.reconcile(m::relinkShelfItem, [](const m::MacRelinkItem&) { return m::RelinkRecordCheck{m::RelinkRecordState::relinked, "/Users/doctor/x"}; }); },
            "Reconcile never accepts a non-Windows path");
    check(m::MacRelinkLedger(destination).find(shelf)->state == m::RelinkState::unresolved, "A refused reconcile writes nothing");
}

// Every WorldMapStore oracle case (Swift-written, v1-v5 and malformed) through
// the import: accepted archives arrive as the exact v4 state the Mac writes
// back after migrating; the others are rejected and write nothing.
void worldMapOracle(const fs::path& fixture) {
    using S = m::MacImportStore;
    const auto document = J::parse(read(fixture), 16 * 1024 * 1024);
    std::size_t accepted{}, rejected{};
    std::set<std::int64_t> versions;
    for (const auto& item : document["cases"].array()) {
        Temporary t;
        Export e{t.root / "export"};
        const auto input = item["input"].string();
        e.add("EndfieldCharge/WorldMap/map.json", input);
        e.write();
        const auto destination = t.root / "EndfieldHUD";
        m::MacImportSession session(e.root, destination, platform());
        session.stage();
        const auto& r = store(session.summary(), S::worldMap);
        if (item["accepted"].boolean()) {
            check(r.status == m::MacImportStatus::imported && session.committable(), "Accepted map imports: " + input + " " + r.detail);
            session.commit();
            const auto saved = J::parse(read(destination / "WorldMap" / "map.json"));
            const auto expected = d::decodeMapArchive(item["snapshot"].string()).snapshot;
            check(saved["version"].integer() == 4 && d::MapStore(destination).value() == expected,
                  "Imported map equals the Mac's migrated state: " + input);
            if (J::parse(input)["version"].integer() == 4) check(read(destination / "WorldMap" / "map.json") == input, "A current-version map arrives byte-identical");
            versions.insert(J::parse(input)["version"].integer());
            ++accepted;
        } else {
            const auto parsed = [&] { try { return J::parse(input); } catch (const std::exception&) { return J(nullptr); } }();
            const bool newer = parsed.isObject() && parsed["version"].isNumber() && parsed["version"].number() == 5;
            check(r.status == (newer ? m::MacImportStatus::rejectedNewer : m::MacImportStatus::rejectedInvalid) && !session.committable(),
                  "Rejected map is reported: " + input + " " + std::string(m::macImportStatusName(r.status)));
            check(!fs::exists(session.workDirectory() / "root" / "WorldMap"), "A rejected map writes nothing");
            ++rejected;
        }
    }
    check(accepted >= 4 && rejected >= 4 && versions == std::set<std::int64_t>{1, 2, 3, 4}, "The oracle covers map versions 1-4 and rejections");
}
void service() {
    Temporary t;
    auto e = fullExport(t.root / "export");
    e.write();
    std::size_t notifications{};
    endfield::app::UtilityExecutor executor([] {});
    m::MacImportService importer(executor, [&] { ++notifications; });
    m::MacImportOptions options; options.acceptRejected = {m::MacImportStore::archive};
    const auto destination = t.root / "dest";
    check(importer.begin(e.root, destination, platform(), options), "Service begins an import");
    check(!importer.begin(e.root, destination, platform(), options), "Only one import at a time");
    check(!importer.requiresFrames() && !importer.nextWakeTime(), "The service never requests frames or timers");
    for (int guard = 0; guard < 200 && importer.state() == m::MacImportService::State::staging; ++guard) { executor.waitIdle(); executor.drain(); }
    {
        const auto progress = importer.progress();
        // open + one verification chunk (the synthetic export is far below 8 MiB)
        // + one step per store + one managed note image batch.
        check(importer.state() == m::MacImportService::State::ready && importer.completedSteps() == 1 + 1 + m::macImportStores.size() + 1 && importer.summary() && importer.committable(),
              std::string("Service stages in bounded steps: ") + std::string(m::macImportServiceStateName(importer.state())) + " " + importer.error());
        check(progress.phase == m::MacImportPhase::staged && progress.exportBytes > 0 && progress.verifiedBytes == progress.exportBytes &&
              progress.validatedStores == m::macImportStores.size(), "Service reports staging progress from the owner thread");
    }
    const auto stagedSteps = importer.completedSteps();
    check(importer.commit(), "Service commits on request");
    for (int guard = 0; guard < 20 && importer.state() == m::MacImportService::State::committing; ++guard) { executor.waitIdle(); executor.drain(); }
    check(importer.state() == m::MacImportService::State::completed && importer.result() && fs::exists(destination / "Migration" / "import.json") && notifications >= 3,
          "Service completes and notifies on the owner thread");
    // refresh, carry, report, activate, clean up
    check(importer.completedSteps() == stagedSteps + 5 && importer.progress().phase == m::MacImportPhase::committed, "Commit runs as bounded steps too");
    // Cancel while ready discards the private folder on the worker.
    m::MacImportService second(executor);
    const auto other = t.root / "other";
    second.begin(e.root, other, platform(), options);
    for (int guard = 0; guard < 200 && second.state() == m::MacImportService::State::staging; ++guard) { executor.waitIdle(); executor.drain(); }
    check(!second.begin(e.root, other, platform(), options), "A reviewed import must be committed or cancelled before another begins");
    second.cancel();
    for (int guard = 0; guard < 20 && second.busy(); ++guard) { executor.waitIdle(); executor.drain(); }
    check(second.state() == m::MacImportService::State::cancelled && !fs::exists(other) && siblingsClean(other), "Cancelled service import leaves nothing behind");
    {   // Cancelling a commit before activation leaves the root unchanged.
        m::MacImportService fifth(executor);
        const auto target = t.root / "cancel-commit";
        write(target / "WindowsOnly" / "keep.txt", "keep");
        const auto before = tree(target);
        check(fifth.begin(e.root, target, platform(), options), "Fifth import begins");
        for (int guard = 0; guard < 200 && fifth.state() == m::MacImportService::State::staging; ++guard) { executor.waitIdle(); executor.drain(); }
        check(fifth.commit(), "Fifth import commits");
        fifth.cancel();
        for (int guard = 0; guard < 20 && fifth.busy(); ++guard) { executor.waitIdle(); executor.drain(); }
        check(fifth.state() == m::MacImportService::State::cancelled && tree(target) == before && siblingsClean(target) && backups(target) == 0,
              std::string("A cancelled commit leaves the root unchanged: ") + std::string(m::macImportServiceStateName(fifth.state())));
    }
    {   // Destroying the owner mid-import drops queued steps; nothing commits.
        auto third = std::make_unique<m::MacImportService>(executor);
        const auto abandoned = t.root / "abandoned";
        check(third->begin(e.root, abandoned, platform(), options), "Third import begins");
        third.reset();
        executor.waitIdle();
        executor.drain();
        check(!fs::exists(abandoned) && siblingsClean(abandoned), "An abandoned import leaves no destination and no private folder");
    }
    {   // Destroying the owner of a reviewed (ready) import removes its folder on the worker.
        auto fourth = std::make_unique<m::MacImportService>(executor);
        const auto reviewed = t.root / "reviewed";
        check(fourth->begin(e.root, reviewed, platform(), options), "Fourth import begins");
        for (int guard = 0; guard < 200 && fourth->state() == m::MacImportService::State::staging; ++guard) { executor.waitIdle(); executor.drain(); }
        check(fourth->state() == m::MacImportService::State::ready && !siblingsClean(reviewed), "Reviewed import holds its private folder");
        fourth.reset();
        executor.waitIdle();
        check(siblingsClean(reviewed) && !fs::exists(reviewed), "Owner shutdown removes the reviewed folder without committing");
    }
    executor.shutdown();
}
}
int main(int argc, char** argv) {
    try {
        codecs();
        streamCopy();
        fullImport();
        migrationsAndRejections();
        wholeImportFailures();
        existingDestination();
        boundedSteps();
        reviewChangesKept();
        earlierRelinkDecisions();
        diskSpaceAndCancellation();
        ledgerReconcile();
        if (argc < 2) throw std::runtime_error("Missing the WorldMapStore oracle fixture argument");
        worldMapOracle(fs::absolute(argv[1]));
        service();
        std::cout << "PASS " << checks << " Mac import orchestration checks; temporary synthetic exports only\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
