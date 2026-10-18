#pragma once
// windowsUpdates area (app/module_owner.hpp, "Adding a module area";
// registered from cmake/modules/packaging.cmake). Owns the Windows updater
// (packaging/updater.hpp) for the single Application owner and publishes
// app::UpdateService (app/areas/update_service.hpp) for the About page and the
// tray. It presents no HUD module and draws nothing.
//
//   production       WinHTTP transport (completions through a private WM_APP
//                    notice; its WinHTTP session exists only while a check or
//                    download runs and opens on the shared queue), the
//                    shared UtilityExecutor, <data root>/Updates,
//                    the installed-copy check of this executable, real
//                    verification and the Setup launcher. An isolated data root
//                    (--data-root) never checks automatically and never installs.
//   hidden coverage  the same updater over synthetic GitHub routes, a synthetic
//                    setup built in the area's coverage root (verified by the
//                    real payload reader), a recording launcher and a synthetic
//                    wall clock: no network, process, registry or user data.
//   development      absent.
#include "app/application.hpp"
#include "app/areas/update_service.hpp"
#include "packaging/updater.hpp"
#include <memory>

namespace endfield::areas {
std::unique_ptr<app::ModuleOwner> windowsUpdatesModule(const app::ApplicationModuleContext&);
} // namespace endfield::areas
