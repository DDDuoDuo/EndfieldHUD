#pragma once
// Windows-only "Import macOS data…" flow (WINDOWS-MIGRATION.md section 7):
// the portable state machine behind the import card of the macImport area
// (app/areas/mac_import_module.hpp). The Mac app has no import feature, so
// the card reuses the HUD confirmation style (HUDQuitConfirmationView card,
// buttons and timings; app/areas/mac_import_card.hpp) and this flow supplies
// its content in the five HUD languages:
//
//   intro -> choosing (shared folder dialog) -> staging (bounded steps on the
//   shared UtilityExecutor, progress) -> [replace: an earlier import exists]
//   -> review (per data set validation report; explicit consent to skip a
//   rejected store) -> restarting (production: every owner saves and closes,
//   the commit runs while nothing is open, the next run reopens the root) or
//   committing (in-process, synthetic destinations only) -> report (final
//   statuses, backup, follow-ups) <-> relink list -> hidden.
//
// No timer, thread or poll: the service notifies on the owner thread
// (UtilityExecutor drain), the host answers the dialog and the restart.
#include "app/utility_executor.hpp"
#include "core/localization.hpp"
#include "core/migration/mac_import_outcome.hpp"
#include "core/migration/mac_import_service.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::areas {
// Visible list rows of the import card (one per data set on the review).
inline constexpr std::size_t macImportCardRows = 12;

enum class MacImportTone : std::uint8_t { primary, secondary, accent, alert };
struct MacImportRow {
    std::string label, value;
    MacImportTone tone{MacImportTone::primary};
    bool operator==(const MacImportRow&) const = default;
};
// Everything the card draws; equal content draws identically.
struct MacImportCardContent {
    std::string title, message, detail, position;
    std::array<std::string, 2> buttons;        // left (cancel/back), right (confirm)
    std::array<bool, 2> enabled{true, true};
    std::array<MacImportRow, macImportCardRows> rows{};
    std::optional<double> progress;            // 0...1 while a bounded job runs
    bool operator==(const MacImportCardContent&) const = default;
};

enum class MacImportStep { hidden, intro, choosing, staging, cancelling, replace, review, restarting, committing, report, relink, failed };
// What the flow holds of an import (WINDOWS-MIGRATION.md section 9). Hidden
// and idle, it holds nothing: equal to a default value. While the card is
// open it holds what the step shows (the session while staging, reviewing or
// committing; the outcome, its rows and relink entries on the final cards).
struct MacImportFlowResidency {
    bool session{};          // a MacImportSession (the service's or a handed-off one)
    bool results{};          // the service's summary, result or error text
    bool outcome{};          // the final report or failure
    std::size_t rows{};      // capacity of the list behind the card's rows
    std::size_t relink{};    // capacity of the relink entries
    std::size_t text{};      // notice, detail and export path bytes
    bool operator==(const MacImportFlowResidency&) const = default;
};
std::string_view macImportStepName(MacImportStep) noexcept;

// Five-language strings of this Windows-only flow (no Mac source exists). The
// Settings System row uses macImportEntryTitle.
std::string macImportEntryTitle(core::Language);
std::string macImportStoreTitle(ehud::migration::MacImportStore, core::Language);

class MacImportFlow final {
public:
    struct Host {
        // Shows the shared folder dialog; the answer arrives in folderChosen().
        // False when the dialog is busy or unavailable.
        std::function<bool(const std::string& title, const std::string& accept)> chooseFolder;
        std::function<void()> cancelChoice;
        // True: commit in this run with progress (only for a destination no
        // other owner has open: hidden coverage's synthetic root). Otherwise
        // (production) `restart` hands the reviewed session to the restart
        // handoff: the commit runs after every owner closed its stores and the
        // next run shows the recorded outcome. False from `restart` (or no
        // `restartAvailable`) keeps the reviewed import.
        std::function<bool()> inProcess;
        std::function<bool(std::shared_ptr<ehud::migration::MacImportSession>)> restart;
        std::function<bool()> restartAvailable;
        std::function<void()> forgetOutcome;   // the recorded outcome was shown and dismissed
        std::function<void()> changed;         // content or state changed: one frame
        std::function<double()> unixNow;
    };
    MacImportFlow(app::UtilityExecutor&, std::filesystem::path destination,
                  std::function<ehud::migration::MacImportPlatform()> platform, Host);
    ~MacImportFlow();
    MacImportFlow(const MacImportFlow&) = delete;
    MacImportFlow& operator=(const MacImportFlow&) = delete;

    // Settings entry: shows the intro, or the running import's state.
    void open();
    bool presented() const noexcept { return step_ != MacImportStep::hidden; }
    MacImportStep step() const noexcept { return step_; }
    // Card answers. Button 0 is the left (cancel/back) action, 1 the right.
    void activate(unsigned button);
    void escape();                           // the left action when it is enabled
    bool scroll(std::ptrdiff_t rows);        // list scrolling; true when it moved
    void folderChosen(bool cancelled, bool failed, std::vector<std::string> paths);
    void utilityCompleted();                 // shared queue capacity was freed
    void restartDeclined();                  // the host kept running (a save failed)
    // A recorded outcome of the previous run (production restart handoff) and
    // the unresolved relink entries of the imported root.
    void showOutcome(ehud::migration::MacImportOutcome, std::vector<ehud::migration::MacRelinkItem> relink);
    void setLanguage(core::Language);
    // Owner shutdown: queued staging/commit steps are dropped (the service's
    // own guarantees) and a handed-off session is released to its holder.
    void shutdown() noexcept;

    const MacImportCardContent& content() const noexcept { return content_; }
    std::uint64_t revision() const noexcept { return revision_; }
    bool busy() const noexcept;              // a service step or a discard is queued/running
    const ehud::migration::MacImportService& service() const noexcept { return *service_; }
    const std::optional<ehud::migration::MacImportOutcome>& outcome() const noexcept { return outcome_; }
    std::size_t listSize() const noexcept { return list_.size(); }
    // The whole list behind the visible rows (UI Automation lists every row).
    const std::vector<MacImportRow>& list() const noexcept { return list_; }
    std::size_t firstRow() const noexcept { return first_; }
    bool outcomeRecorded() const noexcept { return recorded_; }
    const std::filesystem::path& exportRoot() const noexcept { return exportRoot_; }
    MacImportFlowResidency residency() const noexcept;

private:
    void serviceChanged();
    void begin(bool replace);
    void confirm();
    void cancelImport();
    void finishCommit();
    void failed(ehud::migration::MacImportOutcome::State, std::optional<ehud::migration::MacImportErrorCode>, std::string error);
    void hide();
    void choose();
    bool inProcess() const;
    // Rebuilds the card content; `list` false keeps the list (scrolling only).
    void rebuild(bool list = true);
    void buildList();
    void reviewRows();
    void reportRows();
    void relinkRows();
    void discardHandedOff();

    app::UtilityExecutor& utility_;
    app::UtilityExecutor::Route route_{};
    std::filesystem::path destination_, exportRoot_;
    std::function<ehud::migration::MacImportPlatform()> platform_;
    Host host_;
    std::unique_ptr<ehud::migration::MacImportService> service_;
    std::shared_ptr<ehud::migration::MacImportSession> handedOff_;
    std::shared_ptr<bool> alive_;
    MacImportStep step_{MacImportStep::hidden};
    core::Language language_{core::Language::english};
    std::optional<ehud::migration::MacImportOutcome> outcome_;
    std::vector<ehud::migration::MacRelinkItem> relink_;
    std::vector<MacImportRow> list_;
    std::size_t first_{};
    std::string notice_, detail_;
    bool replace_{}, recorded_{}, discarding_{}, declined_{};
    MacImportCardContent content_;
    std::uint64_t revision_{};
};
} // namespace endfield::areas
