#pragma once
// Host-facing services of the macImport area (app/areas/mac_import_module.hpp),
// exchanged through the ApplicationHost area service directory
// (ApplicationHost::publish/find, exact type).
//
//  * MacImportEntry is published BY THE AREA under macImportEntryService. The
//    Settings System page row "Import macOS data…" (macImportEntryTitle(),
//    app/areas/mac_import_flow.hpp, five languages) looks it up when the row
//    is activated and calls present().
//  * DataRootRestart is published BY THE HOST under dataRootRestartService
//    (production; optional in a development preview; never in hidden
//    coverage, where the area commits into its own synthetic destination).
//    It quiesces every owner the only way that closes every store: the
//    accepted-quit path, then a fresh run over the same data root.
#include "core/localization.hpp"
#include <functional>
#include <string>
#include <string_view>

namespace endfield::app {
class MacImportEntry {
public:
    // Shows the import card above the HUD (or the running import's state).
    // False when the area cannot show it now (e.g. disabled).
    virtual bool present(double time) = 0;
    // The Settings row title (macImportEntryTitle in the current language).
    virtual std::string title(core::Language) const = 0;
protected:
    ~MacImportEntry() = default;
};
inline constexpr std::string_view macImportEntryService = "macImport.entry";

class DataRootRestart {
public:
    // Ends this run through the normal accepted-quit path (finishEditing, the
    // flush chain, every save queue, the UtilityExecutor drained and joined,
    // every owner destroyed, the HUD window destroyed). Then, before any new
    // run opens the data root and while the single-instance guard is still
    // held, `afterShutdown` runs exactly once on the main thread, and the host
    // starts a fresh run over the same root so every owner reopens it.
    // When the quit does not happen (a save failed and the quit was cancelled,
    // or the session ended first), `afterShutdown` is destroyed unrun and
    // `declined` runs on the UI thread instead (not during destruction).
    // Returns false (keeping neither function) when a restart cannot be
    // requested now: development/coverage hosts without it, quitting already,
    // a session end in progress or another restart pending.
    virtual bool restartWith(std::function<void()> afterShutdown, std::function<void()> declined) = 0;
protected:
    ~DataRootRestart() = default;
};
inline constexpr std::string_view dataRootRestartService = "application.dataRootRestart";
} // namespace endfield::app
