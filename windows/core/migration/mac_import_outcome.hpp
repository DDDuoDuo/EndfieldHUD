#pragma once
#include "core/migration/mac_import.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ehud::migration {
// The result of one import commit as the import UI reports it afterwards.
//
// In production the commit runs after EndfieldHUD closed every store (the
// restart handoff: the Application is torn down, the commit runs, a new run
// reopens the data root), so the result must outlive the run that started it.
// It is written next to the data root as
//   <parent of root>/<root name>.mac-import-outcome.json
// and read back by the import owner of the next run, which shows the final
// report once and then removes the note. The note holds no record content:
// store statuses and counts, the importer's error text, the source version and
// the backup folder (the user's own path).
struct MacImportOutcomeStore {
    MacImportStore store{};
    MacImportStatus status{MacImportStatus::pending};
    std::size_t records{}, relinkItems{};
    bool operator==(const MacImportOutcomeStore&) const = default;
};
struct MacImportOutcome {
    enum class State { completed, failed, cancelled };
    State state{State::failed};
    std::optional<MacImportErrorCode> code;   // failed/cancelled: the importer's error category
    std::string error;                        // failed: the importer's (English) detail
    std::string sourceVersion, sourceBuild;
    std::string backup;                       // UTF-8 path of the previous data; empty when none was made
    std::vector<MacImportOutcomeStore> stores;// staged/committed statuses (empty when staging never finished)
    std::size_t relinkItems{};
    std::vector<std::string> warnings;
    bool profileSyncLocked{}, launchAtLogin{true}, customShortcutUntranslated{}, remindersNeedReconcile{}, replacesPreviousImport{};
    double finishedAt{};                      // Unix seconds
    bool operator==(const MacImportOutcome&) const = default;
};
std::string_view macImportOutcomeStateName(MacImportOutcome::State) noexcept;
std::string_view macImportErrorCodeName(MacImportErrorCode) noexcept;

// A completed commit (summary as refreshed at commit time).
MacImportOutcome completedMacImportOutcome(const MacImportSummary&, const MacImportCommitResult&, double finishedAt);
// A failed or cancelled import; `summary` (when staged) supplies the statuses.
MacImportOutcome failedMacImportOutcome(MacImportOutcome::State, std::optional<MacImportErrorCode>, std::string error,
                                        const MacImportSummary* summary, double finishedAt);

inline constexpr std::size_t macImportOutcomeMaximumBytes = 1024 * 1024;
std::string encodeMacImportOutcome(const MacImportOutcome&);
// Throws MacImportError (invalid, or newerVersion for a later format).
MacImportOutcome decodeMacImportOutcome(std::string_view);

std::filesystem::path macImportOutcomePath(const std::filesystem::path& destination);
// Replaces any earlier note atomically (temporary file + rename in the same
// folder). Throws on I/O failure.
void writeMacImportOutcome(const std::filesystem::path& destination, const MacImportOutcome&);
// nullopt when there is no note; throws MacImportError for an unreadable one.
std::optional<MacImportOutcome> readMacImportOutcome(const std::filesystem::path& destination);
bool removeMacImportOutcome(const std::filesystem::path& destination) noexcept;

// Runs every remaining commit step of a reviewed session synchronously (the
// caller guarantees that no EndfieldHUD store under `destination` is open),
// then records the outcome next to the root. Never throws: every failure is
// reported in the returned outcome (and the note when it could be written).
// A failure before activation leaves the destination exactly as it was.
MacImportOutcome commitMacImportSession(MacImportSession&, const std::filesystem::path& destination, double finishedAt) noexcept;
}
