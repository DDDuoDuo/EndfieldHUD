#pragma once
// App shortcuts / + Add App module area (app/module_owner.hpp, "Adding a
// module area"; registered by cmake/modules/app_shortcut.cmake).
//
// Mac sources: AppShortcutStore, AppShortcutCanvas, AppShortcutLauncher, OverlayController.requestAppShortcutLaunch /
// completeAppShortcutHandoff and HUDDesktopWatchNavigation.
//
// The owner presents core::Module::addApp with the redesigned canvas
// (modules/app_shortcut_canvas.hpp, Mac 1.3.0) on tools::DesignOwnerCore in
// its own ModuleOwner (drops, coverage and the navigation service are this
// area's; it marks + Add App as a design screen), keeps
// AppShortcuts/shortcuts.json in the data root through ONE AppShortcutTasks
// transaction owner on the shared UtilityExecutor (initial load, inspection,
// save, remove, launch; the canvas's synchronous Mac store calls become its
// requests), chooses applications through the host's shared file dialog,
// extracts each App icon shortcut's icon (the tile image, its trimmed
// side-list copy and the tile colour) and publishes the saved apps for the
// right-side navigation (app/areas/app_shortcut_navigation.hpp).
// A launch is close-first: the target is reinspected, the HUD closes, and the
// application starts only after the close finished (overlayConcealed); a
// failure is shown in Add App (its Notice toast), which reopens. An imported
// Mac shortcut relinks through Open (the chooser) instead of launching. Sleep, session end or quit
// cancel a launch that has not started. No timer, thread or poll.
// Windows-only: after its load and each committed save/remove the same
// transaction owner reconciles the Mac import's Migration/relink.json rows of
// app shortcuts (a relinked shortcut resolves its row, a removed one
// dismisses it; modules/app_shortcut_relink.hpp).
//
// Production: real Windows inspection/launch (native/app_shortcut_service),
// .exe/.lnk icon resources and packaged logos (native/app_shortcut_icons).
// Hidden coverage: synthetic applications under the area's coverage root, a
// synthetic packaged logo fixture, the import's own ledger rows for the seeded
// archive and a counting launcher; the real dialog, the real lifecycle and
// installed applications are never used.
#include "app/application.hpp"
#include <memory>

namespace endfield::areas {
std::unique_ptr<app::ModuleOwner> appShortcutModule(const app::ApplicationModuleContext&);
}
