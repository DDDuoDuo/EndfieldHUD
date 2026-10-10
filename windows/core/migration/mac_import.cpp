#include "core/migration/mac_import.hpp"
#include "core/data/event_log_store.hpp"
#include "core/data/file_io.hpp"
#include "core/data/map_store.hpp"
#include "core/migration/mac_import_files.hpp"
#include "core/migration/mac_import_sha256.hpp"
#include "modules/calendar_repository.hpp"
#include "modules/reader_repository.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <deque>
#include <limits>
#include <set>
#ifdef _WIN32
#include <winsqlite/winsqlite3.h>
#else
#include <sqlite3.h>
#endif

namespace ehud::migration {
namespace {
using data::Json;
namespace fs = std::filesystem;
[[noreturn]] void fail(MacImportErrorCode code, const std::string& message) { throw MacImportError(code, message); }
void need(bool condition, const std::string& message) { if (!condition) fail(MacImportErrorCode::invalid, message); }
std::string show(const fs::path& path) { const auto text = path.u8string(); return std::string(reinterpret_cast<const char*>(text.data()), text.size()); }
fs::path utf8Path(std::string_view text) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())); }
fs::path relative(const std::string& manifestPath) {
    fs::path out;
    std::size_t start = 0;
    while (start <= manifestPath.size()) {
        auto end = manifestPath.find('/', start);
        if (end == std::string::npos) end = manifestPath.size();
        out /= utf8Path(std::string_view(manifestPath).substr(start, end - start));
        start = end + 1;
        if (end == manifestPath.size()) break;
    }
    return out;
}
constexpr std::uint64_t MiB = 1024 * 1024;
struct StoreLayout { MacImportStore store; const char* top; const char* file; std::uint64_t limit; };
// Relative Mac export paths, the Windows top-level name each store owns, and
// the per-file caps of the corresponding Windows codecs.
constexpr StoreLayout layouts[]{
    {MacImportStore::settings, "settings.json", nullptr, 16 * MiB},
    {MacImportStore::notes, "Notes", "EndfieldCharge/Notes/notes.sqlite3", 2048 * MiB},
    {MacImportStore::archive, "Archive", "EndfieldCharge/Archive/archive.sqlite", 2048 * MiB},
    {MacImportStore::profile, "Profile", "EndfieldCharge/Profile/profile.json", 4 * MiB},
    {MacImportStore::fileShelf, "FileShelf", "EndfieldCharge/FileShelf/shelf.json", 4 * MiB},
    {MacImportStore::reader, "Reader", "EndfieldCharge/Reader/library.json", endfield::modules::readerMaximumLibraryBytes},
    {MacImportStore::calendar, "Calendar", "EndfieldCharge/Calendar/calendar.json", endfield::modules::calendarMaximumBytes},
    {MacImportStore::worldMap, "WorldMap", "EndfieldCharge/WorldMap/map.json", data::MapStore::maximumArchiveBytes},
    {MacImportStore::appShortcuts, "AppShortcuts", "EndfieldCharge/AppShortcuts/shortcuts.json", 4 * MiB},
    {MacImportStore::eventLog, "EventLog", "EndfieldCharge/EventLog/events.json", data::EventLogStore::maximumArchiveBytes},
    {MacImportStore::account, "Account", "EndfieldCharge/Account/profile-cache.json", macAccountCacheMaximumBytes},
    {MacImportStore::centerLogo, "CenterLogo", nullptr, 4 * MiB},
};
const StoreLayout& layout(MacImportStore store) {
    for (const auto& entry : layouts) if (entry.store == store) return entry;
    fail(MacImportErrorCode::invalid, "Unknown store");
}
// Managed-image caps. The Mac writes Notes PNGs from a 1600-pixel thumbnail,
// profile backgrounds from 2048 pixels, the center logo at <= 768 pixels and
// keeps original avatar bytes up to 128 MiB (NotesStore/UserProfileStore/
// HUDCenterLogoStore), so these bounds never reject what the Mac produced.
constexpr std::uint64_t notesImageLimit = 16 * MiB, avatarLimit = 128 * MiB, backgroundLimit = 64 * MiB, logoLimit = 4 * MiB;
constexpr std::string_view notesImages = "EndfieldCharge/Notes/Images/", profileImages = "EndfieldCharge/Profile/Images/", logoImages = "EndfieldCharge/CenterLogo/";
// Settings keys ConfigurationStore owns; everything else in an existing
// Windows record is a Windows-only choice (display, hotkey, best score).
const std::set<std::string, std::less<>>& macSettingKeys() {
    static const std::set<std::string, std::less<>> keys{"displayMode", "displayDuration", "accentHex", "theme", "scale", "placement",
        "customScreenID", "customPositionX", "customPositionY", "language", "hudScale", "hudOffsetX", "hudOffsetY", "parallaxIntensity",
        "perspectiveIntensity", "backgroundDarkness", "blurAmount", "reduceMotion", "ambientAnimation", "closeOnFocusLost",
        "openOnActiveDisplay", "hudDisplayUUID", "hudDisplayName", "launchAtLogin", "batteryAlertsEnabled", "devicePopupEnabled",
        "lowPowerVisualMode", "applicationIcon", "clockFormat", "clockStyle", "centerLogo", "centerLogoRevision", "alertMetric", "summonShortcut"};
    return keys;
}
std::int64_t versionOf(std::string_view bytes, std::size_t limit, std::int64_t newest = 1) {
    Json document;
    try { document = Json::parse(bytes, limit); } catch (const std::exception&) { fail(MacImportErrorCode::invalid, "Saved JSON is malformed"); }
    need(document.isObject() && document["version"].isNumber(), "Saved JSON has no version");
    std::int64_t version{};
    try { version = document["version"].integer(); } catch (const std::exception&) { fail(MacImportErrorCode::invalid, "Saved JSON version is not an integer"); }
    if (version > newest) fail(MacImportErrorCode::newerVersion, "Saved by a newer app version; the export was left unchanged");
    return version;
}
struct Database {
    sqlite3* db{};
    explicit Database(const fs::path& path) {
        const auto text = show(path);
        if (sqlite3_open_v2(text.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
            const std::string message = db ? sqlite3_errmsg(db) : "SQLite could not open the database";
            close();
            fail(MacImportErrorCode::invalid, "Database cannot be opened: " + message);
        }
#ifdef SQLITE_DBCONFIG_DEFENSIVE
        sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
#endif
#ifdef _WIN32
        sqlite3_enable_load_extension(db, 0);
#endif
    }
    ~Database() { close(); }
    void close() noexcept { if (db) sqlite3_close_v2(db); db = nullptr; }
    std::string text(const char* sql) {
        sqlite3_stmt* statement{};
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) fail(MacImportErrorCode::invalid, std::string("Database check failed: ") + sqlite3_errmsg(db));
        std::string result;
        const int step = sqlite3_step(statement);
        if (step == SQLITE_ROW) {
            const auto* raw = sqlite3_column_text(statement, 0);
            if (raw) result.assign(reinterpret_cast<const char*>(raw), static_cast<std::size_t>(sqlite3_column_bytes(statement, 0)));
        }
        sqlite3_finalize(statement);
        if (step != SQLITE_ROW) fail(MacImportErrorCode::invalid, std::string("Database check failed: ") + sqlite3_errmsg(db));
        return result;
    }
};
// Full integrity check on the staged copy (normal launches use quick_check).
std::int64_t verifyDatabase(const fs::path& path, std::int64_t newest, std::optional<std::int64_t> declared) {
    Database database(path);
    const auto integrity = database.text("PRAGMA integrity_check");
    need(integrity == "ok", "Database integrity check failed: " + integrity.substr(0, 200));
    const auto version = std::stoll(database.text("PRAGMA user_version"));
    if (declared) need(*declared == version, "Exported database version differs from the manifest");
    if (version > newest) fail(MacImportErrorCode::newerVersion, "Database was saved by a newer app version");
    need(version >= 0, "Invalid database version");
    return version;
}
std::string timestamp(double unix) {
    const auto seconds = static_cast<std::time_t>(unix);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &utc);
    return buffer;
}
std::string megabytes(std::uint64_t bytes) { return std::to_string((bytes + MiB - 1) / MiB); }
MacRelinkItem relinkItem(const MacImportPlatform& platform, std::string_view store, std::string recordID, const Json& reference,
                         std::string displayName, std::string kind, std::optional<std::size_t> index = {}) {
    MacRelinkItem item;
    item.id = platform.makeUUID();
    item.store = std::string(store);
    item.recordID = std::move(recordID);
    item.index = index;
    item.displayName = std::move(displayName);
    item.lastKnownPath = reference["lastKnownPath"].isString() ? reference["lastKnownPath"].string() : reference["path"].isString() ? reference["path"].string() : std::string{};
    item.kind = std::move(kind);
    item.macReference = reference;
    return item;
}
// The store whose Windows records a relink entry points into.
std::optional<MacImportStore> relinkOwner(std::string_view store) {
    if (store == relinkNotesMedia) return MacImportStore::notes;
    if (store == relinkArchiveMedia) return MacImportStore::archive;
    if (store == relinkReaderBook) return MacImportStore::reader;
    if (store == relinkAppShortcut) return MacImportStore::appShortcuts;
    if (store == relinkShelfItem) return MacImportStore::fileShelf;
    return std::nullopt;
}
bool importedStatus(MacImportStatus status) { return status == MacImportStatus::imported || status == MacImportStatus::importedWithWarnings; }
std::optional<std::uint64_t> defaultAvailableBytes(const fs::path& path) {
    std::error_code error;
    const auto info = fs::space(path, error);
    if (error) return std::nullopt;
    return static_cast<std::uint64_t>(info.available);
}
}

std::string_view macImportStoreName(MacImportStore store) noexcept {
    switch (store) {
    case MacImportStore::settings: return "settings";
    case MacImportStore::notes: return "notes";
    case MacImportStore::archive: return "archive";
    case MacImportStore::profile: return "profile";
    case MacImportStore::fileShelf: return "fileShelf";
    case MacImportStore::reader: return "reader";
    case MacImportStore::calendar: return "calendar";
    case MacImportStore::worldMap: return "worldMap";
    case MacImportStore::appShortcuts: return "appShortcuts";
    case MacImportStore::eventLog: return "eventLog";
    case MacImportStore::account: return "account";
    case MacImportStore::centerLogo: return "centerLogo";
    }
    return "unknown";
}
std::string_view macImportStatusName(MacImportStatus status) noexcept {
    switch (status) {
    case MacImportStatus::pending: return "pending";
    case MacImportStatus::imported: return "imported";
    case MacImportStatus::importedWithWarnings: return "importedWithWarnings";
    case MacImportStatus::notInExport: return "notInExport";
    case MacImportStatus::rejectedNewer: return "rejectedNewer";
    case MacImportStatus::rejectedInvalid: return "rejectedInvalid";
    }
    return "unknown";
}
std::string_view macImportPhaseName(MacImportPhase phase) noexcept {
    switch (phase) {
    case MacImportPhase::opening: return "opening";
    case MacImportPhase::verifying: return "verifying";
    case MacImportPhase::validating: return "validating";
    case MacImportPhase::staged: return "staged";
    case MacImportPhase::carrying: return "carrying";
    case MacImportPhase::activating: return "activating";
    case MacImportPhase::committed: return "committed";
    case MacImportPhase::discarded: return "discarded";
    }
    return "unknown";
}
const MacImportStoreReport& MacImportSummary::store(MacImportStore which) const {
    for (const auto& report : stores) if (report.store == which) return report;
    fail(MacImportErrorCode::invalid, "Store was not planned");
}
std::vector<MacImportStore> MacImportSummary::rejected() const {
    std::vector<MacImportStore> out;
    for (const auto& report : stores)
        if (report.status == MacImportStatus::rejectedNewer || report.status == MacImportStatus::rejectedInvalid) out.push_back(report.store);
    return out;
}
bool macImportCompleted(const fs::path& destination) {
    return files::ordinaryFileSize(destination / "Migration" / "import.json").has_value();
}

struct MacImportSession::Impl {
    // One bounded piece of work. Units bound to a store map codec failures onto
    // that store's report (a rejection); all other failures fail the import.
    struct Unit { std::optional<MacImportStore> store; std::function<void()> run; };
    struct Verification { std::size_t file{}; bool copy{}; };
    fs::path exportRoot, destination, work, source, stage, backupPath;
    MacImportPlatform platform;
    MacImportOptions options;
    MacExportManifest manifest;
    MacImportSummary summary;
    MacImportProgress progress;
    std::string manifestBytes, preferencesBytes;
    std::optional<MacSettingsImport> macSettings;   // mapped from the export, before Windows-only choices are merged
    std::set<std::string> written;                  // Windows top-level names produced by the import
    std::vector<std::string> ignoredFiles;
    std::deque<Unit> units;
    std::vector<Verification> verifications;
    std::size_t nextVerification{};
    std::vector<std::string> noteImages;            // unique managed names referenced by imported notes
    std::size_t nextNoteImage{};
    std::vector<files::PlannedFile> carryFiles;
    std::size_t nextCarry{};
    std::unique_ptr<files::StreamCopy> copy;        // the file currently being verified or carried
    std::optional<MacImportCommitResult> result;
    bool opened{}, staged{}, commitStarted{}, activated{}, finished{}, discarded{};

    double now() const {
        if (platform.unixNow) return platform.unixNow();
        return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    void hook(MacImportCommitStep step) { if (options.beforeCommitStep) options.beforeCommitStep(step); }
    std::uint64_t stepBudget() const { return std::max<std::uint64_t>(1, options.stepBytes); }
    MacImportStoreReport& report(MacImportStore store) {
        for (auto& entry : summary.stores) if (entry.store == store) return entry;
        fail(MacImportErrorCode::invalid, "Store was not planned");
    }
    void requireSpace(std::uint64_t bytes, const char* purpose) {
        const auto available = platform.availableBytes ? platform.availableBytes(destination.parent_path()) : std::nullopt;
        if (!available) return;
        const auto needed = bytes + macImportFreeSpaceMargin;
        if (*available < needed)
            fail(MacImportErrorCode::unavailable, std::string("Not enough free disk space ") + purpose + ": about " + megabytes(needed) +
                 " MB are needed next to " + show(destination) + " and " + megabytes(*available) + " MB are free");
    }
    // The cap a store applies to one listed file; nullopt for files no store reads.
    std::optional<std::uint64_t> importLimit(const std::string& path) const {
        if (manifest.preferences && path == *manifest.preferences) return layout(MacImportStore::settings).limit;
        for (const auto& entry : layouts) if (entry.file && path == entry.file) return entry.limit;
        if (path.starts_with(notesImages)) return notesImageLimit;
        if (path.starts_with(profileImages)) return avatarLimit;
        if (path.starts_with(logoImages)) return logoLimit;
        return std::nullopt;
    }

    // ---------------- staging ----------------
    void open() {
        need(exportRoot.is_absolute() && destination.is_absolute(), "Import paths must be absolute");
        need(destination.lexically_normal() == destination && destination != destination.root_path() && destination.has_filename(), "Destination must be an explicit app data folder");
        files::requireOrdinaryDirectory(exportRoot);
        files::requireOrdinaryDirectory(destination.parent_path());
        const auto existing = files::ordinaryDirectory(destination);
        if (!existing && fs::exists(fs::symlink_status(destination))) fail(MacImportErrorCode::invalid, "Destination is not an ordinary directory");
        summary.destinationExisted = existing;
        if (existing && macImportCompleted(destination)) {
            if (!options.replaceExistingImport) fail(MacImportErrorCode::conflict, "macOS data was already imported here; choose replace to import again (a new backup is kept)");
            summary.replacesPreviousImport = true;
        }
        manifestBytes = files::readFile(exportRoot / "manifest.json", 8 * MiB);
        manifest = decodeMacExportManifest(manifestBytes);
        summary.manifestSHA256 = manifest.sha256;
        summary.sourceVersion = manifest.appVersion;
        summary.sourceBuild = manifest.build;
        std::uint64_t total{}, copied{};
        for (std::size_t index = 0; index < manifest.files.size(); ++index) {
            const auto& file = manifest.files[index];
            need(file.bytes <= options.maximumExportBytes - std::min(total, options.maximumExportBytes), "Export exceeds the import size limit");
            total += file.bytes;
            // Every listed file is verified. Files a store may read are copied
            // into the private tree; the others are only checked in place.
            const auto limit = importLimit(file.path);
            const bool copy = limit && file.bytes <= *limit;
            if (copy) copied += file.bytes;
            verifications.push_back({index, copy});
        }
        progress.exportBytes = total;
        for (const auto store : macImportStores) { MacImportStoreReport entry; entry.store = store; summary.stores.push_back(std::move(entry)); }
        classifyIgnored();
        // Existing Windows data the export does not replace is carried at
        // commit. Refuse links anywhere in it now, and reserve room for the
        // copies before writing anything.
        requireSpace(copied + (existing ? carryEstimate() : 0), "for the import");
        const auto token = platform.makeUUID();
        work = destination.parent_path() / utf8Path(show(destination.filename()) + ".import-" + token);
        std::error_code error;
        if (!fs::create_directory(work, error) || error) fail(MacImportErrorCode::unavailable, "Cannot create the private import folder next to " + show(destination));
        source = work / "source";
        stage = work / "root";
        fs::create_directories(stage, error);
        if (error) fail(MacImportErrorCode::unavailable, "Cannot create the staging folder");
        opened = true;
        progress.phase = verifications.empty() ? MacImportPhase::validating : MacImportPhase::verifying;
        if (!verifications.empty()) units.push_back({std::nullopt, [this] { verifyStep(); }});
        for (const auto store : macImportStores) units.push_back({store, [this, store] { storeStep(store); }});
    }
    std::uint64_t carryEstimate() const {
        std::set<std::string, std::less<>> replaced{"Migration"};
        if (manifest.preferences) replaced.insert("settings.json");
        for (const auto& entry : layouts)
            if (entry.file && entry.store != MacImportStore::fileShelf && manifest.find(entry.file)) replaced.insert(entry.top);
        if (std::any_of(manifest.files.begin(), manifest.files.end(), [](const auto& file) { return file.path.starts_with(logoImages); })) replaced.insert("CenterLogo");
        std::error_code error;
        std::vector<fs::path> entries;
        for (fs::directory_iterator it(destination, error), end; !error && it != end; it.increment(error)) entries.push_back(it->path());
        if (error) fail(MacImportErrorCode::unavailable, "Cannot list the existing Windows data");
        std::uint64_t total{};
        for (const auto& entry : entries) {
            std::uint64_t bytes{};
            if (files::ordinaryDirectory(entry)) { for (const auto& file : files::planTree(entry, {}, std::numeric_limits<std::uint64_t>::max())) bytes += file.bytes; }
            else bytes = files::ordinaryFileSize(entry).value_or(0); // throws for a link or reparse point
            if (!replaced.contains(show(entry.filename()))) total += bytes;
        }
        return total;
    }
    void classifyIgnored() {
        for (const auto& file : manifest.files)
            if (!importLimit(file.path)) ignoredFiles.push_back(file.path);
        summary.ignoredFiles = ignoredFiles;
    }
    // Verifies (and copies) listed files, at most stepBytes per step. A damaged,
    // incomplete, linked or tampered export fails the whole import.
    void verifyStep() {
        try {
            auto budget = stepBudget();
            while (nextVerification < verifications.size() && budget) {
                const auto& entry = verifications[nextVerification];
                const auto& file = manifest.files[entry.file];
                if (!copy) {
                    const auto rel = relative(file.path);
                    files::requireOrdinaryParents(exportRoot, rel);
                    const auto from = exportRoot / rel;
                    const auto size = files::ordinaryFileSize(from);
                    if (!size) fail(MacImportErrorCode::corrupt, "Exported file is missing: " + file.path);
                    if (*size != file.bytes) fail(MacImportErrorCode::corrupt, "Exported file size differs from the manifest: " + file.path);
                    copy = std::make_unique<files::StreamCopy>(from, entry.copy ? source / rel : fs::path{}, file.bytes,
                                                              files::StreamCopy::Expected{file.bytes, file.sha256});
                }
                const auto used = copy->advance(budget);
                progress.verifiedBytes += used;
                budget -= std::min(budget, used);
                if (copy->done()) { copy.reset(); ++nextVerification; }
            }
        } catch (const MacImportError& error) {
            if (error.code() == MacImportErrorCode::invalid || error.code() == MacImportErrorCode::tooLarge) fail(MacImportErrorCode::corrupt, error.what());
            throw;
        }
        if (nextVerification < verifications.size()) units.push_front({std::nullopt, [this] { verifyStep(); }});
        else progress.phase = MacImportPhase::validating;
    }
    // The verified private copy of one listed file.
    fs::path ingested(const MacExportFile& file, std::uint64_t limit) {
        if (file.bytes > limit) fail(MacImportErrorCode::tooLarge, file.path + " exceeds its import limit");
        const auto path = source / relative(file.path);
        if (!files::ordinaryFileSize(path)) fail(MacImportErrorCode::corrupt, "Exported file was not verified: " + file.path);
        return path;
    }
    void rejectJournals(const std::string& database) {
        for (const char* suffix : {"-wal", "-journal", "-shm"}) {
            const auto sibling = database + suffix;
            need(!manifest.find(sibling) && !fs::exists(fs::symlink_status(exportRoot / relative(sibling))),
                 "A live SQLite journal accompanies " + database + "; export again with EndfieldHUD closed");
        }
    }
    void addRelink(MacImportStoreReport& report, std::vector<MacRelinkItem> items) {
        report.relinkItems += items.size();
        for (auto& item : items) summary.relink.push_back(std::move(item));
    }
    void warn(MacImportStoreReport& report, std::string message) { report.warnings.push_back(std::move(message)); }

    // Merges the choices made on this PC into the mapped macOS settings and
    // writes the staged settings.json through the unchanged SettingsStore.
    // Runs at staging (for the review) and again at commit, so a Windows-only
    // display or hotkey chosen while the import was reviewed is kept.
    void applySettings(MacImportStoreReport& r) {
        auto mapped = *macSettings;
        r.warnings.clear();
        bool launched = mapped.hasLaunched.value_or(false);
        if (files::ordinaryDirectory(destination) && files::ordinaryFileSize(destination / "settings.json")) {
            try {
                const data::SettingsStore existing(destination);
                const auto& old = existing.value().fields;
                for (const auto& [key, value] : old.object()) {
                    if (macSettingKeys().contains(key)) continue;
                    if (key == "orbipom.bestScore.v1" && value.isNumber() && mapped.settings.fields.contains(key)) {
                        if (value.integer() > mapped.settings.fields[key].integer()) mapped.settings.fields[key] = value;
                        continue;
                    }
                    // Windows-only choices (display, hotkey) made on this PC win over
                    // values derived from macOS defaults; other keys keep the Mac value.
                    if (mapped.settings.fields.contains(key) && !key.starts_with("windows")) continue;
                    if (mapped.settings.fields.contains(key) && mapped.settings.fields[key] == value) continue;
                    mapped.settings.fields[key] = value;
                    mapped.report.push_back({key, MacSettingOutcome::imported, "Kept from the existing Windows settings"});
                    if (key == "windowsSummonShortcut") mapped.customShortcutUntranslated = false;
                }
                const auto envelope = Json::parse(files::readFile(destination / "settings.json", 4 * MiB), 4 * MiB);
                launched = launched || envelope["hasLaunched"] == Json(true);
            } catch (const std::exception&) {
                warn(r, "Existing Windows settings could not be read; only macOS settings were used");
            }
        }
        files::removeTree(stage / "settings.json");
        files::removeTree(stage / "settings.json.lock");
        {
            data::SettingsStore store(stage);
            store.update(mapped.settings);
        }
        if (launched) {
            // SettingsStore keeps AppDelegate's first-run marker in its envelope
            // beside the record; unchanged envelope keys survive later saves.
            const auto path = stage / "settings.json";
            const auto current = data::detail::readFile(path, 4 * MiB);
            auto envelope = Json::parse(*current, 4 * MiB);
            envelope["hasLaunched"] = true;
            data::detail::replaceFile(path, current, envelope.encode(4 * MiB), 4 * MiB);
            (void)data::SettingsStore(stage); // the unchanged store still loads it
        }
        summary.hasLaunched = launched;
        r.records = mapped.report.size();
        summary.settings = mapped.report;
        summary.launchAtLogin = mapped.launchAtLogin;
        summary.customShortcutUntranslated = mapped.customShortcutUntranslated;
        if (mapped.customShortcutUntranslated) warn(r, "The custom macOS summon shortcut cannot be translated; choose a Windows shortcut in Settings");
        warn(r, std::string("Launch at login is ") + (mapped.launchAtLogin ? "on" : "off") + "; it is applied through the Windows startup setting");
    }
    void settingsStep(MacImportStoreReport& r) {
        if (!manifest.preferences) { r.status = MacImportStatus::notInExport; return; }
        const auto path = ingested(*manifest.find(*manifest.preferences), layout(MacImportStore::settings).limit);
        preferencesBytes = files::readFile(path, 16 * MiB);
        PlistDocument document;
        try { document = decodePlist(preferencesBytes); } catch (const PlistError& e) { fail(MacImportErrorCode::invalid, std::string("Preferences are not a valid property list: ") + e.what()); }
        if (!platform.settingsText.prefix) fail(MacImportErrorCode::invalid, "Settings import needs the installed Unicode rules");
        macSettings = mapMacSettings(document.root, platform.settingsText);
        written.insert("settings.json");
        applySettings(r);
        files::writeNewFile(stage / "Migration" / "mac-preferences.plist", preferencesBytes);
    }
    void notesStep(MacImportStoreReport& r) {
        const auto* database = manifest.find(layout(MacImportStore::notes).file);
        if (!database) { r.status = MacImportStatus::notInExport; return; }
        rejectJournals(database->path);
        const auto copied = ingested(*database, layout(MacImportStore::notes).limit);
        const auto target = stage / "Notes" / "notes.sqlite3";
        written.insert("Notes");
        files::moveFile(copied, target);
        const auto it = manifest.sqliteUserVersions.find(database->path);
        verifyDatabase(target, 2, it == manifest.sqliteUserVersions.end() ? std::nullopt : std::optional(it->second));
        std::vector<data::Note> notes;
        { data::NotesStore store(stage); notes = store.notes(); } // migrates v0/v1 inside the stage only
        r.records = notes.size();
        std::set<std::string> referenced;
        std::vector<MacRelinkItem> relink;
        for (const auto& note : notes) {
            if (note.imageName && referenced.insert(*note.imageName).second) noteImages.push_back(*note.imageName);
            if (note.media) {
                const auto media = Json::parse(*note.media, 16 * MiB);
                if (media["referencePlatform"].isNull())
                    relink.push_back(relinkItem(platform, relinkNotesMedia, note.id, media, media["displayName"].isString() ? media["displayName"].string() : std::string{},
                                                media["kind"].isString() ? media["kind"].string() : std::string("image")));
            }
        }
        std::size_t orphans{};
        for (const auto& file : manifest.files)
            if (file.path.starts_with(notesImages) && !referenced.contains(file.path.substr(notesImages.size()))) ++orphans;
        if (orphans) warn(r, std::to_string(orphans) + " unreferenced managed image(s) were not imported");
        if (!relink.empty()) warn(r, std::to_string(relink.size()) + " external media reference(s) need relinking");
        addRelink(r, std::move(relink));
        if (!noteImages.empty()) units.push_front({MacImportStore::notes, [this] { notesImagesStep(); }});
    }
    // Managed note images in bounded batches: copied byte-for-byte after the
    // PNG header (and on Windows a WIC decode) checks out. A missing or
    // unusable image is reported and its note keeps the reference.
    void notesImagesStep() {
        auto& r = report(MacImportStore::notes);
        std::size_t checked{};
        std::uint64_t bytes{};
        while (nextNoteImage < noteImages.size() && checked < std::max<std::size_t>(1, options.stepImages) && bytes < stepBudget()) {
            const auto& name = noteImages[nextNoteImage++];
            ++checked;
            const auto* file = manifest.find(std::string(notesImages) + name);
            if (!file) { warn(r, "Managed image " + name + " is missing; the note keeps its reference"); continue; }
            if (file->bytes > notesImageLimit) { warn(r, "Managed image " + name + " exceeds 16 MiB; it was not copied"); continue; }
            bytes += file->bytes;
            const auto image = ingested(*file, notesImageLimit);
            if (!pngHeader(files::readPrefix(image, 64))) { warn(r, "Managed image " + name + " is not a PNG; it was not copied"); continue; }
            if (platform.decodeImage) if (const auto error = platform.decodeImage(image)) { warn(r, "Managed image " + name + " does not decode (" + *error + "); it was not copied"); continue; }
            files::moveFile(image, stage / "Notes" / "Images" / utf8Path(name));
        }
        if (nextNoteImage < noteImages.size()) units.push_front({MacImportStore::notes, [this] { notesImagesStep(); }});
    }
    void archiveStep(MacImportStoreReport& r) {
        const auto* database = manifest.find(layout(MacImportStore::archive).file);
        if (!database) { r.status = MacImportStatus::notInExport; return; }
        rejectJournals(database->path);
        const auto copied = ingested(*database, layout(MacImportStore::archive).limit);
        const auto target = stage / "Archive" / "archive.sqlite";
        written.insert("Archive");
        files::moveFile(copied, target);
        const auto it = manifest.sqliteUserVersions.find(database->path);
        verifyDatabase(target, 2, it == manifest.sqliteUserVersions.end() ? std::nullopt : std::optional(it->second));
        if (!platform.archive) fail(MacImportErrorCode::invalid, "Archive validation needs the installed Unicode rules");
        auto relink = platform.archive(stage / "Archive");
        for (auto& item : relink) if (item.id.empty()) item.id = platform.makeUUID();
        r.records = 1;
        if (!relink.empty()) warn(r, std::to_string(relink.size()) + " Archive attachment(s) need relinking");
        addRelink(r, std::move(relink));
    }
    void profileStep(MacImportStoreReport& r) {
        const auto* json = manifest.find(layout(MacImportStore::profile).file);
        if (!json) { r.status = MacImportStatus::notInExport; return; }
        const auto copied = ingested(*json, layout(MacImportStore::profile).limit);
        versionOf(files::readFile(copied, 4 * MiB), 4 * MiB);
        written.insert("Profile");
        files::copyFile(copied, stage / "Profile" / "profile.json", json->bytes);
        data::Profile profile;
        { const data::ProfileStore store(stage); profile = store.value(); } // the file exists: never invents a profile
        r.records = 1;
        for (const auto& [name, avatar] : {std::pair{profile.avatarFilename, true}, std::pair{profile.backgroundFilename, false}}) {
            if (!name) continue;
            const auto kind = std::string(avatar ? "Avatar" : "Background");
            const auto target = stage / "Profile" / "Images" / utf8Path(*name);
            if (files::ordinaryFileSize(target)) continue; // both fields name one file
            const auto* file = manifest.find(std::string(profileImages) + *name);
            if (!file) { warn(r, kind + " image " + *name + " is missing; the profile keeps its reference"); continue; }
            const auto limit = avatar ? avatarLimit : backgroundLimit;
            if (file->bytes > limit) { warn(r, kind + " image " + *name + " exceeds " + megabytes(limit) + " MiB; it was not copied"); continue; }
            const auto image = ingested(*file, limit);
            const auto header = files::readPrefix(image, 64);
            if (name->ends_with(".png") && !pngHeader(header)) { warn(r, "Profile image " + *name + " is not a PNG; it was copied unchanged"); }
            else if (sniffImage(header) == ImageSignature::unknown) warn(r, "Profile image " + *name + " has an unrecognized format; it was copied unchanged");
            if (platform.decodeImage) if (const auto error = platform.decodeImage(image))
                warn(r, "Profile image " + *name + " cannot be decoded on this PC (" + *error + "); its original bytes were kept");
            // Original encoded bytes are owned data; the codec is never inferred from ".image".
            files::moveFile(image, target);
        }
    }
    void shelfStep(MacImportStoreReport& r) {
        const auto* json = manifest.find(layout(MacImportStore::fileShelf).file);
        if (!json) { r.status = MacImportStatus::notInExport; return; }
        const auto copied = ingested(*json, layout(MacImportStore::fileShelf).limit);
        const auto records = decodeMacShelf(files::readFile(copied, 4 * MiB));
        r.records = records.size();
        std::vector<MacRelinkItem> relink;
        for (const auto& record : records)
            relink.push_back(relinkItem(platform, relinkShelfItem, record.id, record.original, record.name, record.isDirectory ? "folder" : "file"));
        if (!relink.empty()) warn(r, std::to_string(relink.size()) + " shelf item(s) need relinking; the Windows shelf starts empty until then");
        addRelink(r, std::move(relink));
        // Nothing is written to FileShelf/: an existing Windows shelf is kept.
    }
    void readerStep(MacImportStoreReport& r) {
        const auto* json = manifest.find(layout(MacImportStore::reader).file);
        if (!json) { r.status = MacImportStatus::notInExport; return; }
        const auto copied = ingested(*json, layout(MacImportStore::reader).limit);
        versionOf(files::readFile(copied, layout(MacImportStore::reader).limit), layout(MacImportStore::reader).limit);
        written.insert("Reader");
        files::copyFile(copied, stage / "Reader" / "library.json", json->bytes);
        endfield::modules::ReaderJSONRepository repository(stage / "Reader");
        const auto library = repository.load();
        r.records = library.books.size();
        std::vector<MacRelinkItem> relink;
        for (const auto& book : library.books)
            if (book.platform == endfield::modules::ReaderReferencePlatform::macOS)
                relink.push_back(relinkItem(platform, relinkReaderBook, book.id, Json(Json::Object{{"bookmark", book.bookmarkBase64}, {"path", book.path}, {"scoped", book.scoped}}),
                                            book.title, "book"));
        if (!relink.empty()) warn(r, std::to_string(relink.size()) + " book(s) need relinking; progress and bookmarks are kept");
        addRelink(r, std::move(relink));
    }
    void calendarStep(MacImportStoreReport& r) {
        const auto* json = manifest.find(layout(MacImportStore::calendar).file);
        if (!json) { r.status = MacImportStatus::notInExport; return; }
        const auto copied = ingested(*json, layout(MacImportStore::calendar).limit);
        versionOf(files::readFile(copied, layout(MacImportStore::calendar).limit), layout(MacImportStore::calendar).limit);
        written.insert("Calendar");
        files::copyFile(copied, stage / "Calendar" / "calendar.json", json->bytes);
        if (!platform.calendarText.characters || !platform.calendarText.trimmed || !platform.calendarText.prefix)
            fail(MacImportErrorCode::invalid, "Calendar validation needs the installed Unicode rules");
        endfield::modules::CalendarJSONRepository repository(stage / "Calendar", platform.calendarText);
        auto file = repository.load();
        // beforeScheduledFor/dayScheduledFor are receipts of the macOS
        // notification center. The reconcile (Mac and Windows alike) never
        // replays a receipted reminder missing from the pending OS schedule, so
        // kept receipts would silently drop every future reminder on Windows.
        // Past reminders stay excluded by the plan itself.
        std::size_t receipts{};
        for (auto& event : file.events) {
            receipts += static_cast<std::size_t>(event.beforeScheduledFor.has_value()) + static_cast<std::size_t>(event.dayScheduledFor.has_value());
            event.beforeScheduledFor.reset();
            event.dayScheduledFor.reset();
        }
        if (receipts) repository.save(file);
        r.records = file.events.size();
        summary.remindersNeedReconcile = !file.events.empty();
        if (!file.events.empty()) warn(r, "Reminder notifications are registered again on Windows after import");
        if (receipts) warn(r, std::to_string(receipts) + " macOS notification receipt(s) were cleared so Windows schedules those reminders");
    }
    void mapStep(MacImportStoreReport& r) {
        const auto* json = manifest.find(layout(MacImportStore::worldMap).file);
        if (!json) { r.status = MacImportStatus::notInExport; return; }
        const auto copied = ingested(*json, layout(MacImportStore::worldMap).limit);
        (void)versionOf(files::readFile(copied, layout(MacImportStore::worldMap).limit), layout(MacImportStore::worldMap).limit, 4); // WorldMapStore writes v4, reads 1-3
        written.insert("WorldMap");
        files::copyFile(copied, stage / "WorldMap" / "map.json", json->bytes);
        const data::MapStore store(stage); // v1-3 migrate to v4 inside the stage only
        r.records = store.value().pins.size();
    }
    void shortcutsStep(MacImportStoreReport& r) {
        const auto* json = manifest.find(layout(MacImportStore::appShortcuts).file);
        if (!json) { r.status = MacImportStatus::notInExport; return; }
        const auto copied = ingested(*json, layout(MacImportStore::appShortcuts).limit);
        const auto bytes = files::readFile(copied, layout(MacImportStore::appShortcuts).limit);
        versionOf(bytes, layout(MacImportStore::appShortcuts).limit);
        written.insert("AppShortcuts");
        files::copyFile(copied, stage / "AppShortcuts" / "shortcuts.json", json->bytes);
        if (!platform.appShortcuts) fail(MacImportErrorCode::invalid, "App shortcut validation needs the installed Unicode rules");
        auto relink = platform.appShortcuts(stage / "AppShortcuts");
        for (auto& item : relink) if (item.id.empty()) item.id = platform.makeUUID();
        r.records = Json::parse(bytes, layout(MacImportStore::appShortcuts).limit)["items"].array().size();
        if (!relink.empty()) warn(r, std::to_string(relink.size()) + " app shortcut(s) need a Windows app; names, icons and order are kept");
        addRelink(r, std::move(relink));
    }
    void eventLogStep(MacImportStoreReport& r) {
        const auto* json = manifest.find(layout(MacImportStore::eventLog).file);
        if (!json) { r.status = MacImportStatus::notInExport; return; }
        const auto copied = ingested(*json, layout(MacImportStore::eventLog).limit);
        versionOf(files::readFile(copied, layout(MacImportStore::eventLog).limit), layout(MacImportStore::eventLog).limit);
        written.insert("EventLog");
        files::copyFile(copied, stage / "EventLog" / "events.json", json->bytes);
        const data::EventLogStore store(stage);
        if (const auto status = store.statusMessage()) fail(MacImportErrorCode::invalid, std::string(*status));
        r.records = store.events().size();
    }
    void accountStep(MacImportStoreReport& r) {
        const auto* json = manifest.find(layout(MacImportStore::account).file);
        if (!json) { r.status = MacImportStatus::notInExport; return; }
        const auto copied = ingested(*json, layout(MacImportStore::account).limit);
        const auto result = importMacAccountCache(files::readFile(copied, macAccountCacheMaximumBytes));
        written.insert("Account");
        files::writeNewFile(stage / "Account" / "profile-cache.json", result.bytes);
        r.records = result.roles;
        summary.profileSyncLocked = result.profileSyncLocked;
        for (const auto& region : result.disconnectedRegions) warn(r, "Sign in again to reconnect the " + region + " account; cached roles stay visible");
        if (!result.strippedKeys.empty()) warn(r, std::to_string(result.strippedKeys.size()) + " credential-like field(s) were removed and never imported");
    }
    void centerLogoStep(MacImportStoreReport& r) {
        const auto revision = macSettings ? macSettings->centerLogoRevision : std::nullopt;
        std::size_t others{};
        for (const auto& file : manifest.files) if (file.path.starts_with(logoImages)) ++others;
        if (!revision) {
            r.status = others ? MacImportStatus::imported : MacImportStatus::notInExport;
            if (others) r.detail = "No custom logo is selected; stored logo files were not imported";
            return;
        }
        const auto name = *revision + ".png";
        const auto* file = manifest.find(std::string(logoImages) + name);
        if (!file) { warn(r, "The custom center logo file is missing; the Endfield logo is shown until a new one is chosen"); return; }
        if (file->bytes > logoLimit) { warn(r, "The custom center logo is not a valid managed PNG; the Endfield logo is shown"); return; }
        const auto image = ingested(*file, logoLimit);
        const auto header = pngHeader(files::readPrefix(image, 64));
        if (!header || header->width > 768 || header->height > 768) { warn(r, "The custom center logo is not a valid managed PNG; the Endfield logo is shown"); return; }
        if (platform.decodeImage) if (const auto error = platform.decodeImage(image)) { warn(r, "The custom center logo does not decode (" + *error + "); the Endfield logo is shown"); return; }
        written.insert("CenterLogo");
        files::moveFile(image, stage / "CenterLogo" / utf8Path(name));
        r.records = 1;
        if (others > 1) warn(r, std::to_string(others - 1) + " unused logo revision(s) were not imported");
    }
    void storeStep(MacImportStore store) {
        ++progress.validatedStores;
        auto& r = report(store);
        r.replacesWindowsData = summary.destinationExisted && store != MacImportStore::fileShelf &&
            fs::exists(fs::symlink_status(destination / layout(store).top));
        switch (store) {
        case MacImportStore::settings: settingsStep(r); break;
        case MacImportStore::notes: notesStep(r); break;
        case MacImportStore::archive: archiveStep(r); break;
        case MacImportStore::profile: profileStep(r); break;
        case MacImportStore::fileShelf: shelfStep(r); break;
        case MacImportStore::reader: readerStep(r); break;
        case MacImportStore::calendar: calendarStep(r); break;
        case MacImportStore::worldMap: mapStep(r); break;
        case MacImportStore::appShortcuts: shortcutsStep(r); break;
        case MacImportStore::eventLog: eventLogStep(r); break;
        case MacImportStore::account: accountStep(r); break;
        case MacImportStore::centerLogo: centerLogoStep(r); break;
        }
        if (r.status == MacImportStatus::notInExport) r.replacesWindowsData = false;
    }
    void reject(MacImportStore store, MacImportStatus status, std::string detail) {
        auto& r = report(store);
        r.status = status;
        r.detail = std::move(detail);
        // A rejected store writes nothing; any existing Windows data is carried.
        r.replacesWindowsData = false;
        r.relinkItems = 0;
        units.erase(std::remove_if(units.begin(), units.end(), [&](const Unit& unit) { return unit.store == store; }), units.end());
        summary.relink.erase(std::remove_if(summary.relink.begin(), summary.relink.end(), [&](const MacRelinkItem& item) { return relinkOwner(item.store) == store; }), summary.relink.end());
        written.erase(layout(store).top);
        files::removeTree(stage / layout(store).top);
        switch (store) {
        case MacImportStore::settings:
            files::removeTree(stage / "settings.json.lock");
            files::removeTree(stage / "Migration" / "mac-preferences.plist");
            macSettings.reset();
            summary.settings.clear();
            summary.launchAtLogin = true;
            summary.customShortcutUntranslated = false;
            summary.hasLaunched = false;
            break;
        case MacImportStore::notes: noteImages.clear(); break;
        case MacImportStore::account: summary.profileSyncLocked = false; break;
        case MacImportStore::calendar: summary.remindersNeedReconcile = false; break;
        default: break;
        }
    }
    void runStaging(Unit& unit) {
        if (!unit.store) { unit.run(); return; }
        const auto store = *unit.store;
        try {
            unit.run();
            return;
        } catch (const MacImportError& error) {
            if (error.code() != MacImportErrorCode::invalid && error.code() != MacImportErrorCode::newerVersion && error.code() != MacImportErrorCode::tooLarge) throw;
            reject(store, error.code() == MacImportErrorCode::newerVersion ? MacImportStatus::rejectedNewer : MacImportStatus::rejectedInvalid, error.what());
        } catch (const data::StoreError& error) {
            reject(store, error.code() == data::StoreErrorCode::newerVersion ? MacImportStatus::rejectedNewer : MacImportStatus::rejectedInvalid, error.what());
        } catch (const std::exception& error) {
            reject(store, MacImportStatus::rejectedInvalid, error.what());
        }
    }
    void finishStaging() {
        for (auto& r : summary.stores)
            if (r.status == MacImportStatus::pending) r.status = r.warnings.empty() ? MacImportStatus::imported : MacImportStatus::importedWithWarnings;
        progress.phase = MacImportPhase::staged;
        staged = true;
    }

    // ---------------- commit (owners have closed their stores) ----------------
    void refreshStep() {
        hook(MacImportCommitStep::refresh);
        progress.phase = MacImportPhase::carrying;
        // The data root may have appeared, changed or vanished during review.
        const auto existing = files::ordinaryDirectory(destination);
        if (!existing && fs::exists(fs::symlink_status(destination))) fail(MacImportErrorCode::invalid, "Destination is not an ordinary directory");
        if (existing && macImportCompleted(destination) && !summary.replacesPreviousImport) {
            if (!options.replaceExistingImport) fail(MacImportErrorCode::conflict, "macOS data was already imported here; choose replace to import again (a new backup is kept)");
            summary.replacesPreviousImport = true;
        }
        summary.destinationExisted = existing;
        backupPath.clear();
        if (existing) {
            const auto name = show(destination.filename()) + ".backup-" + timestamp(now()) + "-" + platform.makeUUID().substr(0, 8);
            backupPath = destination.parent_path() / utf8Path(name);
            need(!fs::exists(fs::symlink_status(backupPath)), "Backup name collision");
        }
        summary.backup = backupPath;
        if (macSettings) {
            auto& r = report(MacImportStore::settings);
            applySettings(r);
            r.status = r.warnings.empty() ? MacImportStatus::imported : MacImportStatus::importedWithWarnings;
        }
        keepEarlierRelinkDecisions();
        planCarry();
        units.push_back({std::nullopt, [this] { carryStep(true); }});
        units.push_back({std::nullopt, [this] { reportStep(); }});
        units.push_back({std::nullopt, [this] { activateStep(); }});
        units.push_back({std::nullopt, [this] { cleanupStep(); }});
    }
    // A repeated import keeps the user's earlier relink entries (and decisions)
    // for data this import does not replace, e.g. Mac shelf items when the new
    // export has no shelf, or Reader books when its library was rejected.
    void keepEarlierRelinkDecisions() {
        if (!summary.destinationExisted || !files::ordinaryFileSize(destination / "Migration" / "relink.json")) return;
        try {
            const MacRelinkLedger earlier(destination);
            std::set<std::string> ids;
            for (const auto& item : summary.relink) ids.insert(item.id);
            std::size_t kept{};
            for (const auto& item : earlier.items()) {
                const auto owner = relinkOwner(item.store);
                if (!owner || importedStatus(report(*owner).status) || !ids.insert(item.id).second) continue;
                summary.relink.push_back(item);
                ++kept;
            }
            if (kept) summary.warnings.push_back(std::to_string(kept) + " earlier relink entr" + (kept == 1 ? "y was" : "ies were") + " kept for data this import does not replace");
        } catch (const std::exception& error) {
            summary.warnings.push_back(std::string("The earlier relink list could not be read (") + error.what() + "); it stays in the backup");
        }
    }
    void planCarry() {
        carryFiles.clear();
        nextCarry = 0;
        progress.carriedBytes = progress.carryBytes = 0;
        if (!summary.destinationExisted) return;
        std::error_code error;
        std::vector<fs::path> entries;
        for (fs::directory_iterator it(destination, error), end; !error && it != end; it.increment(error)) entries.push_back(it->path());
        if (error) fail(MacImportErrorCode::unavailable, "Cannot list the existing Windows data");
        std::sort(entries.begin(), entries.end());
        std::uint64_t total{};
        for (const auto& entry : entries) {
            const auto name = show(entry.filename());
            if (name == "Migration" || written.contains(name) || fs::exists(fs::symlink_status(stage / entry.filename()))) continue;
            const auto remaining = options.maximumExportBytes - std::min(total, options.maximumExportBytes);
            if (files::ordinaryDirectory(entry)) {
                for (auto& file : files::planTree(entry, stage / entry.filename(), remaining)) { total += file.bytes; carryFiles.push_back(std::move(file)); }
            } else if (const auto size = files::ordinaryFileSize(entry)) {
                if (*size > remaining) fail(MacImportErrorCode::tooLarge, "Existing Windows data exceeds the import budget");
                total += *size;
                carryFiles.push_back({entry, stage / entry.filename(), *size});
            }
        }
        progress.carryBytes = total;
        requireSpace(total, "to keep the existing Windows data");
    }
    // Copies existing Windows data the import does not replace, at most
    // stepBytes per step. The owners are closed, so a file that changes size
    // now is a conflict rather than silently truncated or extended data.
    void carryStep(bool first) {
        if (first) hook(MacImportCommitStep::carry);
        try {
            auto budget = stepBudget();
            while (nextCarry < carryFiles.size() && budget) {
                const auto& file = carryFiles[nextCarry];
                if (!copy) copy = std::make_unique<files::StreamCopy>(file.from, file.to, file.bytes);
                const auto used = copy->advance(budget);
                progress.carriedBytes += used;
                budget -= std::min(budget, used);
                if (copy->done()) {
                    if (copy->processed() != file.bytes) fail(MacImportErrorCode::conflict, "Existing Windows data changed during the import; close EndfieldHUD windows and try again");
                    copy.reset();
                    ++nextCarry;
                }
            }
        } catch (const MacImportError& error) {
            if (error.code() == MacImportErrorCode::tooLarge) fail(MacImportErrorCode::conflict, "Existing Windows data changed during the import; close EndfieldHUD windows and try again");
            throw;
        }
        if (nextCarry < carryFiles.size()) units.push_front({std::nullopt, [this] { carryStep(false); }});
    }
    void reportStep() {
        hook(MacImportCommitStep::report);
        Json::Array stores;
        for (const auto& s : summary.stores) {
            Json::Array warnings;
            for (const auto& w : s.warnings) warnings.push_back(w);
            stores.push_back(Json::Object{{"store", std::string(macImportStoreName(s.store))}, {"status", std::string(macImportStatusName(s.status))},
                {"detail", s.detail}, {"warnings", std::move(warnings)}, {"records", static_cast<std::int64_t>(s.records)},
                {"relinkItems", static_cast<std::int64_t>(s.relinkItems)}, {"replacesWindowsData", s.replacesWindowsData}});
        }
        Json::Array settingRows;
        for (const auto& row : summary.settings)
            settingRows.push_back(Json::Object{{"key", row.key}, {"outcome", std::string(macSettingOutcomeName(row.outcome))}, {"detail", row.detail}});
        Json::Array ignored, warnings;
        for (const auto& path : ignoredFiles) ignored.push_back(path);
        for (const auto& warning : summary.warnings) warnings.push_back(warning);
        const Json document = Json::Object{{"version", 1}, {"importedAt", now()}, {"manifestSHA256", summary.manifestSHA256},
            {"source", Json::Object{{"bundleIdentifier", manifest.bundleIdentifier}, {"appVersion", manifest.appVersion}, {"build", manifest.build}, {"exportedAt", manifest.exportedAt}}},
            {"stores", std::move(stores)}, {"settings", std::move(settingRows)}, {"ignoredFiles", std::move(ignored)}, {"warnings", std::move(warnings)},
            {"profileSyncLocked", summary.profileSyncLocked}, {"launchAtLogin", summary.launchAtLogin},
            {"customShortcutUntranslated", summary.customShortcutUntranslated}, {"remindersNeedReconcile", summary.remindersNeedReconcile},
            {"hasLaunched", summary.hasLaunched},
            {"backup", summary.backup.empty() ? Json(nullptr) : Json(show(summary.backup))}};
        files::writeNewFile(stage / "Migration" / "mac-export-manifest.json", manifestBytes);
        files::writeNewFile(stage / "Migration" / "relink.json", MacRelinkLedger::encode(summary.relink));
        files::writeNewFile(stage / "Migration" / "import.json", document.encode(16 * MiB));
    }
    void activateStep() {
        progress.phase = MacImportPhase::activating;
        bool movedAway = false;
        try {
            hook(MacImportCommitStep::backupDestination);
            if (summary.destinationExisted) { files::renameNoReplace(destination, backupPath); movedAway = true; }
            hook(MacImportCommitStep::activateStage);
            files::renameNoReplace(stage, destination);
        } catch (...) {
            if (movedAway) {
                try { files::renameNoReplace(backupPath, destination); }
                catch (...) {
                    fail(MacImportErrorCode::unavailable, "Import failed and the previous data could not be moved back automatically; it is intact at " + show(backupPath));
                }
            }
            throw;
        }
        activated = true;
        result = MacImportCommitResult{destination, backupPath, false};
        progress.phase = MacImportPhase::committed;
    }
    void cleanupStep() {
        try { hook(MacImportCommitStep::removeWork); result->workRemoved = files::removeTree(work); }
        catch (...) { result->workRemoved = false; }
        finished = true;
    }
};

MacImportSession::MacImportSession(fs::path exportRoot, fs::path destination, MacImportPlatform platform, MacImportOptions options)
    : impl_(std::make_unique<Impl>()) {
    impl_->exportRoot = std::move(exportRoot);
    impl_->destination = std::move(destination);
    impl_->platform = std::move(platform);
    impl_->options = std::move(options);
    if (!impl_->platform.makeUUID) impl_->platform.makeUUID = data::makeUUID;
    if (!impl_->platform.availableBytes) impl_->platform.availableBytes = defaultAvailableBytes;
}
MacImportSession::~MacImportSession() { discard(); }
bool MacImportSession::stageNext() {
    auto& i = *impl_;
    if (i.discarded) fail(MacImportErrorCode::cancelled, "The import was discarded");
    if (i.staged) return false;
    if (i.platform.cancelled && i.platform.cancelled()) { discard(); fail(MacImportErrorCode::cancelled, "Import cancelled"); }
    try {
        if (!i.opened) i.open();
        else {
            auto unit = std::move(i.units.front());
            i.units.pop_front();
            i.runStaging(unit);
        }
        if (i.units.empty()) { i.finishStaging(); return false; }
        return true;
    } catch (...) {
        discard();
        throw;
    }
}
void MacImportSession::stage() { while (stageNext()) {} }
bool MacImportSession::staged() const noexcept { return impl_->staged; }
const MacImportSummary& MacImportSession::summary() const noexcept { return impl_->summary; }
MacImportProgress MacImportSession::progress() const noexcept { return impl_->progress; }
const fs::path& MacImportSession::workDirectory() const noexcept { return impl_->work; }
bool MacImportSession::committable() const {
    if (!impl_->staged || impl_->commitStarted || impl_->discarded) return false;
    for (const auto store : impl_->summary.rejected())
        if (std::find(impl_->options.acceptRejected.begin(), impl_->options.acceptRejected.end(), store) == impl_->options.acceptRejected.end()) return false;
    return true;
}
bool MacImportSession::commitNext() {
    auto& i = *impl_;
    if (i.discarded) fail(MacImportErrorCode::cancelled, "The import was discarded");
    if (i.finished) return false;
    if (!i.commitStarted) {
        if (!committable()) fail(MacImportErrorCode::conflict, "The import is not ready to commit; review rejected stores first");
        i.commitStarted = true;
        i.units.push_back({std::nullopt, [&i] { i.refreshStep(); }});
    }
    if (!i.activated && i.platform.cancelled && i.platform.cancelled()) { discard(); fail(MacImportErrorCode::cancelled, "Import cancelled"); }
    auto unit = std::move(i.units.front());
    i.units.pop_front();
    try {
        unit.run();
    } catch (...) {
        // Before activation nothing outside the private folder has changed
        // (a failed activation already moved the previous root back).
        if (!i.activated) discard();
        throw;
    }
    return !i.units.empty();
}
MacImportCommitResult MacImportSession::commit() {
    while (commitNext()) {}
    return *impl_->result;
}
bool MacImportSession::committed() const noexcept { return impl_->activated; }
const std::optional<MacImportCommitResult>& MacImportSession::commitResult() const noexcept { return impl_->result; }
void MacImportSession::discard() noexcept {
    if (!impl_) return;
    auto& i = *impl_;
    i.units.clear();
    i.copy.reset(); // closes handles and removes a partial copy
    if (!i.activated) {
        if (i.discarded) return;
        i.discarded = true;
        i.progress.phase = MacImportPhase::discarded;
    }
    // After activation only the private source copies remain in the work folder.
    if (!i.work.empty() && !i.finished) files::removeTree(i.work);
}
}
