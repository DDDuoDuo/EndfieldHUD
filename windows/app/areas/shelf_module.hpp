#pragma once
// File Shelf coverage area (app/module_owner.hpp, "Adding a module area";
// registered from cmake/modules/shelf.cmake as a "|coverage" area).
//
// The File Shelf is a BUILT-IN production owner: app/application.cpp
// constructShelf() builds tools::ShelfService on the persistent data root and
// registers its DesignModuleOwner as "fileShelf", so EndfieldHUD.exe runs every
// behaviour below through that service. The shared module cycles render that
// owner but cannot rename, move or delete originals without touching user
// data, so this area drives the same production classes in the hidden
// coverage harness on synthetic files only, with its own service, design
// Context (a private DesignOverlayOwner) and owner:
//   - reference import and the Event Log records SystemHUDView wires to the
//     canvas (shelfAdded per new item, shelfRemoved, shelfCleared with count);
//     adding selects nothing;
//   - stale-path detection: a renamed and a moved original are found again by
//     their 128-bit file identity and their new paths are saved (the Mac
//     bookmark renewal), a deleted one and one moved into "$Recycle.Bin" stay
//     unavailable, and every untouched original survives remove and clear;
//   - a Mac-imported Shelf item (an unresolved fileShelf.item entry of the
//     import area's Migration/relink.json) listed as an unavailable tile,
//     relinked from its card to a synthetic file that keeps the imported
//     identifier, with the ledger entry resolved and shelfAdded recorded;
//   - the UI Automation tree of the presented Shelf (Add, Clear, one tab per
//     tile, the card's actions), Select acting like a click, and its
//     withdrawal when the Shelf is hidden;
//   - the properties card's reads on the shared utility worker (never the HUD
//     thread) and Open through the host's action message (a synthetic Shell
//     handoff);
//   - the localized platform names.
// It owns a NEW data root inside its coverage root, a private never-shown
// window (the HUD HWND keeps its own routes), a private rasterizer (outside the
// module cycles' measured raster usage), the shared utility executor, no icon
// worker, no drop registration and no clipboard access. Event Log records go
// to a counting sink: the real Event Log is drawn by the module cycles, whose
// raster usage must repeat exactly. The built-in Shelf's memory (no artwork
// once the HUD closed on another module) is checked by the host's own
// coverage (app/application_verification.cpp). Outside hidden coverage the
// factory returns null (the built-in owner is the production path).
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
// use Add, or paste with Ctrl+V; HUD drops, the picker and paste all use
// the same reference store and Event Log records as the Mac status-item drop.
#include "app/application.hpp"
#include <memory>

namespace endfield::shelf {
std::unique_ptr<app::ModuleOwner> shelfCoverageModule(const app::ApplicationModuleContext&);
}
