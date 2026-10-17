#pragma once
// Ownership of the importer's private work folders (README.md section 2).
//
// MacImportSession stages next to the data root in a folder it creates
// exclusively and marks before writing anything else:
//
//   <parent>/<root>.import-<token>\        token: the session's UUID
//       import-work.json                    marker: format, root name, token;
//                                           held open (locked) by the session
//                                           for its whole lifetime
//       source\  root\                      verified export copies, staged root
//       activation.json                     flushed right before the first
//                                           rename of the activation; names the
//                                           backup the root is moved to
//
// A process that ends while it owns a work folder (killed during staging or
// during the post-shutdown commit of the restart handoff) leaves the folder
// behind. The next run's import owner sweeps such folders, but only folders
// that are provably the importer's own and abandoned:
//   * the name is exactly "<root>.import-<token>" next to the root and the
//     folder is an ordinary directory (never a link or junction);
//   * the marker is an ordinary file whose content names that root and token;
//   * no live owner holds the marker (Windows: share mode; POSIX: flock), so a
//     running import in any process is never touched;
//   * every top-level entry is one the importer writes (anything else, such as
//     a file a user put there, keeps the whole folder).
// An empty folder with such a name (a process ended between creating it and
// writing its marker) holds nothing and is removed with a non-recursive
// remove. Backups (<root>.backup-*), the outcome note, the data root, other
// roots' folders and anything unmarked are never touched. Links and reparse
// points inside a swept folder are removed as entries, never followed. The
// marker is removed last, so an interrupted sweep is resumed by the next one.
//
// An activation interrupted between its two renames (the root already moved
// to its backup, the staged root not yet in place) is never swept: before any
// owner opens the root, recoverMacImportWork() moves the backup back while the
// root is still absent, then the folder is an ordinary abandoned one.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ehud::migration {
inline constexpr std::string_view macImportWorkMarker = "import-work.json";
inline constexpr std::string_view macImportActivationJournal = "activation.json";
inline constexpr std::size_t macImportWorkNoteLimit = 4096;

// "<root>.import-<token>" next to `destination` (an explicit absolute root).
std::filesystem::path macImportWorkPath(const std::filesystem::path& destination, std::string_view token);
// Tokens are 1...64 characters of [0-9A-Za-z-] (MacImportPlatform::makeUUID).
bool validMacImportWorkToken(std::string_view) noexcept;
std::string encodeMacImportWorkMarker(std::string_view rootName, std::string_view token, double createdAt);
// The marker's token when `bytes` is a marker for `rootName`; nullopt otherwise.
std::optional<std::string> decodeMacImportWorkMarker(std::string_view bytes, std::string_view rootName) noexcept;
// Activation journal: the backup folder name ("<root>.backup-..."), or none
// when no root existed. decode: nullopt for an unreadable journal; an empty
// string for "no backup".
std::string encodeMacImportActivation(std::string_view rootName, std::string_view backupName);
std::optional<std::string> decodeMacImportActivation(std::string_view bytes, std::string_view rootName) noexcept;

// The session's claim on its work folder.
class MacImportWorkLock final {
public:
    MacImportWorkLock() noexcept;
    ~MacImportWorkLock();                 // releases the lock; the folder stays unless remove() succeeded
    MacImportWorkLock(MacImportWorkLock&&) noexcept;
    MacImportWorkLock& operator=(MacImportWorkLock&&) noexcept;
    MacImportWorkLock(const MacImportWorkLock&) = delete;
    MacImportWorkLock& operator=(const MacImportWorkLock&) = delete;
    // Creates the folder (it must not exist; never through a link) and its
    // marker, locked by the returned object. Throws MacImportError.
    static MacImportWorkLock create(const std::filesystem::path& destination, std::string_view token, double createdAt);
    const std::filesystem::path& folder() const noexcept;
    bool held() const noexcept;
    // Records the activation's backup name (empty: none) before the first rename.
    void journalActivation(const std::filesystem::path& backup);
    // Removes every entry but the marker, then the marker and the folder, and
    // releases the lock. When anything could not be removed the marker stays
    // (unlocked) so the next sweep finishes the folder. True when nothing is left.
    bool remove() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class MacImportWorkState {
    abandoned,     // never activated: removable
    activated,     // the staged root is in place; only private copies remain: removable
    interrupted,   // the root moved to its backup, the staged root not in place: kept
    live,          // a running import holds it: kept
    unrecognized   // named like a work folder but not provably the importer's: kept
};
std::string_view macImportWorkStateName(MacImportWorkState) noexcept;
struct MacImportWorkFolder {
    std::filesystem::path folder, backup;  // backup: interrupted only
    MacImportWorkState state{MacImportWorkState::unrecognized};
};
// Classifies every folder named like a work folder of `destination` (read
// only; nothing is removed). Never throws; an unreadable parent lists nothing.
std::vector<MacImportWorkFolder> inspectMacImportWork(const std::filesystem::path& destination) noexcept;

// Bounded sweep of the stale (abandoned or activated) work folders of one
// root, for the shared utility worker: next() performs at most `budget`
// filesystem operations (one listing or one removal each). Never throws.
inline constexpr std::size_t macImportSweepBudget = 256;
class MacImportWorkSweep final {
public:
    struct Stats {
        std::size_t candidates{}, removed{}, live{}, interrupted{}, unrecognized{}, failed{};
        std::uint64_t entries{}, steps{};
        bool operator==(const Stats&) const = default;
    };
    explicit MacImportWorkSweep(std::filesystem::path destination);
    ~MacImportWorkSweep();
    MacImportWorkSweep(const MacImportWorkSweep&) = delete;
    MacImportWorkSweep& operator=(const MacImportWorkSweep&) = delete;
    bool next(std::size_t budget = macImportSweepBudget) noexcept; // false once finished
    void run() noexcept;
    bool finished() const noexcept;
    const Stats& stats() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Before any owner opens `destination` (the host's startup, under its
// single-instance guard): every interrupted activation whose root is absent is
// rolled back (backup -> root, no replace) and a failed outcome note records
// it for the next import card. Nothing is removed. Never throws.
struct MacImportRecovery {
    std::size_t restored{}, kept{};       // kept: interrupted while a root exists
    std::vector<std::filesystem::path> backups; // restored backups (now the root)
};
MacImportRecovery recoverMacImportWork(const std::filesystem::path& destination, double finishedAt) noexcept;
}
