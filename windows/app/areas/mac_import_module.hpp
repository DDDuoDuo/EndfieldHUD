#pragma once
// macImport module area (app/module_owner.hpp, "Adding a module area";
// registered by cmake/modules/import.cmake): the Windows-only "Import macOS
// data…" flow of WINDOWS-MIGRATION.md section 7. The Mac app has no import,
// so the area reuses the HUD confirmation style: a modal card above every
// module (app/areas/mac_import_card.hpp) driven by app/areas/mac_import_flow.hpp.
//
//  * Entry: the area publishes app::MacImportEntry (app/areas/
//    mac_import_handoff.hpp); the Settings System row "Import macOS data…"
//    calls present(). The card captures pointer, wheel and keys like the red
//    power button confirmation and follows the source center tilt.
//  * Folder: the host's one shared file dialog (ApplicationHost::chooseFiles).
//  * Staging and validation: MacImportService on the shared UtilityExecutor
//    (bounded steps, progress), then the per data set report.
//  * Commit (production): the reviewed session goes to the host's
//    app::DataRootRestart. The accepted-quit path flushes and closes every
//    owner's store, the commit runs while nothing under the data root is
//    open (backup + same-volume switch, rollback on failure), the outcome is
//    recorded next to the root and the next run reopens every owner from it.
//    That next run's area reads the note and shows the final report and the
//    relink list once. Without the host service the review cannot be
//    confirmed (nothing is written).
//  * Startup sweep: private "<root>.import-<token>" folders that an earlier
//    run left behind (a process that ended while staging or during the
//    post-shutdown commit) are removed in bounded steps on the shared
//    executor, but only folders the importer created and marked and that no
//    live import holds (core/migration/mac_import_work.hpp). Backups, the
//    outcome note, unmarked look-alikes and interrupted activations are never
//    touched; the host's recoverMacImportWork() before opening the root
//    rolls the latter back.
//  * UI Automation: the card is a dialog under this owner's group of the HUD
//    window's root (native/mac_import_automation.hpp): title, message,
//    progress, the full list (scrolled-out rows offscreen), detail and the
//    two buttons (Invoke = the click).
//  * Hidden coverage: the Mac-generated golden export (declared fixture
//    macImport.goldenExport) is imported into the area's own synthetic
//    destination under its coverage root, in-process and through the
//    production restart path against a coverage stand-in of the host's
//    DataRootRestart (published under the same key, which the host leaves
//    free outside production); the real dialog, data root and lifecycle are
//    never used.
// No timer, thread or poll: UtilityExecutor completions (utilityCompleted),
// the dialog completion and the frame clock drive everything.
#include "app/application.hpp"
#include <memory>

namespace endfield::areas {
std::unique_ptr<app::ModuleOwner> macImportModule(const app::ApplicationModuleContext&);
}
