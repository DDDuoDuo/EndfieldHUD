#pragma once
// App shortcuts / + Add App module area (app/module_owner.hpp, "Adding a
// module area"; registered by cmake/modules/app_shortcut.cmake).
//
// Mac sources: AppShortcutStore, AppShortcutCanvas, HUDAppShortcutInteraction,
// AppShortcutLauncher, OverlayController.requestAppShortcutLaunch /
// completeAppShortcutHandoff and HUDDesktopWatchNavigation.
//
// The owner presents core::Module::addApp with the retained source canvas
// (tools/app_shortcut_preview.hpp), keeps AppShortcuts/shortcuts.json in the
// data root through ONE AppShortcutTasks transaction owner on the shared
// UtilityExecutor (initial load, inspection, save, remove, launch), chooses
// applications through the host's shared file dialog, and publishes the saved
// apps for the right-side navigation (app/areas/app_shortcut_navigation.hpp).
// A launch is close-first: the target is reinspected, the HUD closes, and the
// application starts only after the close finished (overlayConcealed); a
// failure is shown in Add App, which reopens. Sleep, session end or quit
// cancel a launch that has not started. No timer, thread or poll.
//
// Production: real Windows inspection/launch (native/app_shortcut_service).
// Hidden coverage: synthetic applications under the area's coverage root and a
// counting launcher; the real dialog, the real lifecycle and installed
// applications are never used.
#include "app/application.hpp"
#include <memory>

namespace endfield::areas {
std::unique_ptr<app::ModuleOwner> appShortcutModule(const app::ApplicationModuleContext&);
}
