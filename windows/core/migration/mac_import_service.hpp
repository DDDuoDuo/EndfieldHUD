#pragma once
#include "app/utility_executor.hpp"
#include "core/migration/mac_import.hpp"
#include <memory>

namespace ehud::migration {
// App-owned, event-driven wrapper that runs one MacImportSession on the shared
// utility worker, one bounded step per job (open, export verification in
// stepBytes chunks, each store, then the commit steps). No timer, poll or
// private thread: progress arrives through the executor's owner-thread
// drain(), and `changed` is invoked there.
//
// Owner protocol (production app): show the Windows-only "Import macOS data"
// entry, call begin() with the user-picked export folder and the versioned data
// root; present summary() when state()==ready (rejected stores, relink count,
// settings outcomes) and progress() while staging or committing. Before
// commit() every module owner must flush and close its stores
// (SettingsSaveQueue, Notes, ArchiveService, ReaderOwner, Calendar, EventLog,
// MapStore, FileShelfStore, profile and account owners); the commit re-reads
// the current root, so nothing saved during the review is lost. After
// `completed` (or `failed`/`cancelled`, which leave the root unchanged) the
// owners reopen the same root and apply profileSyncLocked / launchAtLogin /
// remindersNeedReconcile / customShortcutUntranslated.
class MacImportService final {
public:
    enum class State { idle, staging, ready, committing, completed, failed, cancelled };
    MacImportService(endfield::app::UtilityExecutor&, std::function<void()> changed = {});
    ~MacImportService();
    MacImportService(const MacImportService&) = delete;
    MacImportService& operator=(const MacImportService&) = delete;
    // False while another import is staging, ready or committing. A full shared
    // queue is retried from queueCapacityAvailable(), never from a timer.
    bool begin(std::filesystem::path exportRoot, std::filesystem::path destination, MacImportPlatform, MacImportOptions = {});
    bool commit();                 // only when state()==ready && committable(); owners closed first
    // staging/ready: discards the private work folder. committing: honoured
    // until the new root is activated, then the commit completes.
    void cancel();
    void queueCapacityAvailable(); // resubmits a step that found the queue full
    State state() const noexcept;
    bool committable() const noexcept;
    std::size_t completedSteps() const noexcept;
    MacImportProgress progress() const noexcept;                 // as of the last finished step
    const MacImportSummary* summary() const noexcept;           // valid from ready onward
    const std::optional<MacImportCommitResult>& result() const noexcept;
    const std::string& error() const noexcept;
    bool busy() const noexcept;    // a job is queued or running
    bool requiresFrames() const noexcept { return false; }
    std::optional<double> nextWakeTime() const noexcept { return {}; }
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
std::string_view macImportServiceStateName(MacImportService::State) noexcept;
}
