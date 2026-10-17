#pragma once
// Host-facing service of the windowsUpdates area (app/areas/update_module.hpp),
// published BY THE AREA in the ApplicationHost service directory under
// UpdateService::key (exact type). UI thread only; valid while the area's
// owner is registered, so the host looks it up when used.
//
// Host wiring (Mac AppDelegate.configureUpdater / checkForAppUpdates):
//   About "Check for updates" row  SettingsCallbacks::checkUpdates -> activate();
//                                  a returned URL is opened like any About link
//                                  (after the HUD closed)
//   About toggle                   SettingsCallbacks::automaticUpdates -> setAutomaticChecks()
//   About rows                     SettingsController::setUpdateStatus(settingsStatus(language))
//                                  whenever the observer fires or the language changes
//   Tray "Check for Updates…"      open About, then check() (Mac: About opens for a
//                                  copy that cannot install; it always shows the result)
//   Tray item title / help         trayTitle(language) / trayHelp(language)
// Without this service (development previews, a disabled area) the host keeps
// its fallback: the row opens the GitHub releases page.
#include "core/localization.hpp"
#include "modules/settings.hpp"
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace endfield::app {
class UpdateService {
public:
    static constexpr std::string_view key = "updates.windows";
    // One GitHub metadata check (only releases carrying the Windows
    // installer assets count). Never downloads or installs. False when busy,
    // stopped or not loaded yet (then it runs as soon as the saved state is
    // loaded).
    virtual bool check(double time) = 0;
    // The About update row: check; on an installable update, download and
    // verify it (the click is the consent); on a verified download, close the
    // HUD and hand off to Setup (the click is the consent). For a release this
    // copy cannot install (portable or development copy), returns its GitHub
    // page for the host to open.
    virtual std::optional<std::string> activate(double time) = 0;
    // About toggle: "Check for updates daily" (at most one check per day, when
    // the HUD opens). Persisted by the area; ignored when unavailable.
    virtual void setAutomaticChecks(bool enabled, double time) = 0;
    // The About rows in `language` (title, detail, latest release, its page,
    // toggle state and availability).
    virtual modules::SettingsUpdateStatus settingsStatus(core::Language) const = 0;
    virtual std::string automaticTitle(core::Language) const = 0; // the toggle label on Windows
    virtual std::string trayTitle(core::Language) const = 0;      // "Update available · 1.2.1 (19)…" / "Checking for updates…" / "Check for Updates…"
    virtual std::string trayHelp(core::Language) const = 0;       // detail, else title
    virtual bool hasUpdate() const = 0;
    // One observer (the host's): runs on the UI thread after any change above.
    virtual void setObserver(std::function<void()>) = 0;
protected:
    ~UpdateService() = default;
};
} // namespace endfield::app
