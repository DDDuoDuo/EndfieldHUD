#pragma once
// Saved app shortcuts in the source desktop navigation (Mac
// HUDDesktopWatchNavigation.entries(shortcuts:) and
// HUDSourceWatchView.setDesktopNavigation). The App shortcuts area
// (app/areas/app_shortcut_module.hpp) publishes ONE object of this exact type
// in the host's area service directory under appShortcutNavigationService.
// Header only: the host includes it and needs no link to the area library.
//
// Host contract (UI thread only):
//   * After the module areas exist, look the service up and install one
//     observer. The area calls it after its committed list (or a saved app's
//     title/icon) changed; the host then marks its navigation dirty and
//     requests one frame (no work inside the observer).
//   * Whenever the runtime navigation entries are (re)bound - the localized
//     preference event and a dirty mark - pass the catalog's localized module
//     entries (every source module, Add App included) and the catalog's
//     right-side actions (ending in Add App) to plan(). Bind the returned
//     entries to NativeWatchAppearance BEFORE handing the returned right-side
//     actions to SourceWatchSession::setNavigation (its row layout follows
//     the new count and resets the scroll like the source).
//   * A navigation activation whose action is not a source module goes to
//     activate(); true means it belonged to a saved app and was consumed.
// Saved-app actions are stable across rename, icon edits and reordering of
// other items, never reuse a removed action and never alias a module action.
#include "native/watch_appearance.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace endfield::app {
inline constexpr std::string_view appShortcutNavigationService = "appShortcuts.navigation";

class AppShortcutNavigation {
public:
    struct Plan {
        // Module entries in catalog order, then the saved apps in their stored
        // order (module=false, target "app.<id>", the saved name and
        // "shortcut:<preset>" icon), then Add App. Valid until the next plan().
        std::span<const native::WatchAppearanceEntry> entries;
        // Catalog right-side actions with the saved apps between Power and the
        // always-last Add App.
        std::span<const std::uint64_t> rightActions;
    };
    // Unknown or invalid bases (no Add App last) return the inputs unchanged.
    virtual Plan plan(std::span<const native::WatchAppearanceEntry> modules, std::span<const std::uint64_t> right) = 0;
    // Close-first launch of the saved app bound to `action` (Mac
    // requestAppShortcutLaunch). True when the action belongs to a saved app,
    // whether or not a launch could start (one launch at a time; a missing or
    // imported target shows its error in Add App instead of closing).
    virtual bool activate(std::uint64_t action, double time) = 0;
    // The host's single observer (empty removes it).
    virtual void setObserver(std::function<void()>) = 0;

protected:
    ~AppShortcutNavigation() = default;
};
} // namespace endfield::app
