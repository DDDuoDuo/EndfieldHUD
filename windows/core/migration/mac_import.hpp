#pragma once
#include "core/migration/mac_import_codecs.hpp"
#include "core/migration/mac_import_relink.hpp"
#include "core/migration/mac_import_settings.hpp"
#include "modules/calendar_model.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace ehud::migration {
// One-time offline import of a user-supplied macOS export (WINDOWS-MIGRATION.md
// section 7) into the versioned Windows data root. The export is produced by
// windows/tools/mac_import_exporter.py and is never written. Staging verifies
// every listed file against its manifest size and SHA-256 while copying it
// into a private work directory next to the destination, then validates every
// store with its existing Windows codec on that copy (migrations run there
// only). Commit runs after the module owners flushed and closed their stores:
// it re-reads the current Windows data (so nothing saved during review is
// lost), carries what the import does not replace, writes the report and
// activates the complete new root with one same-volume rename after the
// previous root became a timestamped backup. Any failure leaves the
// destination exactly as it was; backups are never deleted automatically.
// All work runs in bounded steps for the shared utility worker.
enum class MacImportStore { settings, notes, archive, profile, fileShelf, reader, calendar, worldMap, appShortcuts, eventLog, account, centerLogo };
inline constexpr std::array<MacImportStore, 12> macImportStores{MacImportStore::settings, MacImportStore::notes, MacImportStore::archive,
    MacImportStore::profile, MacImportStore::fileShelf, MacImportStore::reader, MacImportStore::calendar, MacImportStore::worldMap,
    MacImportStore::appShortcuts, MacImportStore::eventLog, MacImportStore::account, MacImportStore::centerLogo};
std::string_view macImportStoreName(MacImportStore) noexcept;
enum class MacImportStatus { pending, imported, importedWithWarnings, notInExport, rejectedNewer, rejectedInvalid };
std::string_view macImportStatusName(MacImportStatus) noexcept;
struct MacImportStoreReport {
    MacImportStore store{};
    MacImportStatus status{MacImportStatus::pending};
    std::string detail;
    std::vector<std::string> warnings;
    std::size_t records{}, relinkItems{};
    bool replacesWindowsData{};
};
// Platform pieces injected by the native owner (see mac_import_native.hpp).
// Missing validators make that store "rejectedInvalid", which blocks commit.
struct MacImportPlatform {
    MacImportTextRules settingsText;
    endfield::modules::CalendarTextRules calendarText;
    // Validate a staged Archive/ or AppShortcuts/ directory through its Windows
    // repository and return the Mac references that need relinking.
    std::function<std::vector<MacRelinkItem>(const std::filesystem::path&)> archive, appShortcuts;
    // Optional full decode check of a managed image (Windows: WIC). Returns an
    // error text, or nullopt when the bytes decode.
    std::function<std::optional<std::string>(const std::filesystem::path&)> decodeImage;
    std::function<std::string()> makeUUID = data::makeUUID;
    std::function<double()> unixNow;    // defaults to the system clock
    std::function<bool()> cancelled;    // polled between bounded steps only
    // Free bytes on the destination volume (defaults to std::filesystem::space).
    // nullopt skips the preflight.
    std::function<std::optional<std::uint64_t>(const std::filesystem::path&)> availableBytes;
};
enum class MacImportCommitStep { refresh, carry, report, backupDestination, activateStage, removeWork };
struct MacImportOptions {
    bool replaceExistingImport{};                    // explicit user choice; a fresh backup is made
    std::vector<MacImportStore> acceptRejected;      // explicit consent to skip these rejected stores
    std::function<void(MacImportCommitStep)> beforeCommitStep; // fault injection for tests
    std::uint64_t maximumExportBytes{8ull * 1024 * 1024 * 1024};
    // Work bound of one step: bytes verified/copied, and managed images checked.
    // One SQLite integrity_check or one store codec load is a single step.
    std::uint64_t stepBytes{8ull * 1024 * 1024};
    std::size_t stepImages{16};
};
// Free space kept on the destination volume beyond what the import writes.
inline constexpr std::uint64_t macImportFreeSpaceMargin = 64ull * 1024 * 1024;
enum class MacImportPhase { opening, verifying, validating, staged, carrying, activating, committed, discarded };
std::string_view macImportPhaseName(MacImportPhase) noexcept;
struct MacImportProgress {
    MacImportPhase phase{MacImportPhase::opening};
    std::uint64_t verifiedBytes{}, exportBytes{};     // every listed file is checked against its manifest digest
    std::size_t validatedStores{};                    // of macImportStores.size()
    std::uint64_t carriedBytes{}, carryBytes{};       // existing Windows data copied at commit
    bool operator==(const MacImportProgress&) const = default;
};
struct MacImportSummary {
    std::vector<MacImportStoreReport> stores;
    std::vector<MacSettingReport> settings;
    std::vector<MacRelinkItem> relink;
    std::vector<std::string> ignoredFiles;            // listed (and verified) files no store reads
    std::vector<std::string> warnings;                // import-wide notes (e.g. an unreadable earlier relink list)
    std::string manifestSHA256, sourceVersion, sourceBuild;
    bool profileSyncLocked{}, launchAtLogin{true}, customShortcutUntranslated{}, remindersNeedReconcile{};
    bool hasLaunched{};                               // first-run onboarding already happened on the Mac
    bool destinationExisted{}, replacesPreviousImport{};
    std::filesystem::path backup;                     // empty when no destination existed
    const MacImportStoreReport& store(MacImportStore) const;
    std::vector<MacImportStore> rejected() const;
};
struct MacImportCommitResult {
    std::filesystem::path destination, backup;
    bool workRemoved{};
};

class MacImportSession final {
public:
    MacImportSession(std::filesystem::path exportRoot, std::filesystem::path destination, MacImportPlatform, MacImportOptions = {});
    ~MacImportSession();
    MacImportSession(const MacImportSession&) = delete;
    MacImportSession& operator=(const MacImportSession&) = delete;
    // Bounded staging steps for the shared utility worker: open, verify the
    // export in stepBytes chunks, validate each store (managed images in
    // stepImages batches). Returns false once staged. Whole-import failures
    // (manifest, digests, unsafe paths, disk space, I/O) throw MacImportError
    // and leave nothing behind; a rejected store does not throw.
    bool stageNext();
    void stage();                                     // all remaining staging steps
    bool staged() const noexcept;
    const MacImportSummary& summary() const noexcept;
    MacImportProgress progress() const noexcept;
    bool committable() const;                         // staged, no unaccepted rejection
    // Bounded commit steps; call only after every module owner flushed and
    // closed its stores. Refreshes the Windows-only settings and the earlier
    // relink decisions from the current root, carries existing data the import
    // does not replace (stepBytes per step), writes Migration/, then activates.
    // Returns false once committed. A failure before activation discards the
    // private folder and leaves the destination untouched; cancellation is
    // honoured until activation. Requires committable() for the first step.
    bool commitNext();
    MacImportCommitResult commit();                   // all remaining commit steps
    bool committed() const noexcept;
    const std::optional<MacImportCommitResult>& commitResult() const noexcept;
    void discard() noexcept;                          // removes the private work directory (never the destination or a backup)
    const std::filesystem::path& workDirectory() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Destination policy: <root>/Migration/import.json exists after a completed import.
bool macImportCompleted(const std::filesystem::path& destination);
}
