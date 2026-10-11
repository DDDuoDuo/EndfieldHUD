#pragma once
// Temporary File Shelf coverage area (app/module_owner.hpp, "Adding a module
// area"; registered from cmake/modules/shelf.cmake as a "|coverage" area).
//
// The File Shelf is a BUILT-IN production owner: app/application.cpp
// constructShelf() builds tools::ShelfPreview on the persistent data root and
// registers it as "fileShelf", so EndfieldHUD.exe runs every behaviour below
// through that owner. The shared module cycles render that owner but cannot
// rename, move or delete originals or open previews without touching user
// data, so this area drives the same production class in the hidden coverage
// harness (given the build's staged package) on synthetic files only:
//   - reference import and the Event Log records SystemHUDView wires to the
//     canvas (shelfAdded per new item, shelfRemoved, shelfCleared with count);
//   - stale-path detection: a renamed and a moved original are found again by
//     their 128-bit file identity and their new paths are saved (the Mac
//     bookmark renewal), a deleted one and one moved into "$Recycle.Bin" stay
//     unavailable, and every untouched original survives remove and clear;
//   - a Mac-imported Shelf item (an unresolved fileShelf.item entry of the
//     import area's Migration/relink.json) listed as an unavailable card,
//     relinked to a synthetic file that keeps the imported identifier, with
//     the ledger entry resolved and shelfAdded recorded;
//   - the UI Automation peers of the presented Shelf (one per accessible
//     action plus the imported card's Relink peer), Invoke selecting a card
//     and asking for the picker, and withdrawal when the Shelf is hidden;
//   - Quick Look's generic preview for a folder and for a type with no
//     registered stream preview handler, in a hidden popup on the dedicated
//     preview thread (never the HUD thread);
//   - preview containment: a synthetic handler type that stalls is cancelled
//     by the preview thread's watchdog (localized "not responding" text), the
//     retired thread ends once released and the next preview runs on a fresh
//     preview thread;
//   - the localized text of those operations.
// It owns a NEW data root inside its coverage root, a private never-shown
// owner window (the HUD HWND keeps its own routes), a private rasterizer for
// its presentation (outside the module cycles' measured raster usage), a
// hidden preview popup, no icon worker, no drop registration and no clipboard
// access. Event Log records go to a counting sink: the real Event Log is drawn
// by the module cycles, whose raster usage must repeat exactly. Outside hidden
// coverage the factory returns null (the built-in owner is the production path).
//
// Recorded platform gap, direct tray-icon drop (Mac StatusItemFileDropView):
// Shell_NotifyIconW offers no drop contract. Its callback carries only mouse,
// keyboard and NIN_* notifications; the notification-area icon (and its
// overflow flyout) is drawn by Explorer's own process (TrayNotifyWnd, or the
// XAML system tray on Windows 11), and RegisterDragDrop accepts only windows of
// the calling process. A drag in progress also holds the mouse in the source's
// DoDragDrop loop, so no hover message reaches the icon's owner. Every
// alternative (injecting into Explorer, a hidden or floating drop window, a
// taskbar button for the tool-window HUD) is excluded by WINDOWS-MIGRATION.md
// section 5. Fallback: open the HUD (tray click or hotkey) and drop on the Shelf,
// use Add files, or paste with Ctrl+V; HUD drops, the picker and paste all use
// the same reference store and Event Log records as the Mac status-item drop.
#include "app/application.hpp"
#include <memory>

namespace endfield::shelf {
std::unique_ptr<app::ModuleOwner> shelfCoverageModule(const app::ApplicationModuleContext&);
}
